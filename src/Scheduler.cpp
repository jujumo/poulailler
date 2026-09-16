#include "Scheduler.h"

#include <Dusk2Dawn.h>
#include <WiFi.h>
#include <esp_sleep.h>

#include "Debug.h"
#include "config.h"  // must come before Arduino.h to override LED_BUILTIN
#include "TimeZone.h"

namespace {

// Fire tolerance for scheduled handleDueActions() calls. Portal polling and
// scheduled alarms both need a small window around the target.
constexpr int kToleranceBeforeSec = -5;
constexpr int kToleranceAfterSec = 5;

// Safety net in case a DS3231 alarm is ever missed/misconfigured.
constexpr uint64_t kFallbackSleepSeconds = 24ULL * 3600ULL;
constexpr uint64_t kDebugSleepFallbackSeconds = 60ULL;

int normalizeMinutes(int minutes) {
    minutes %= 1440;
    if (minutes < 0) minutes += 1440;
    return minutes;
}

// Both arguments are seconds-of-day; target is always at :00 (open/close
// targets are resolved to whole minutes), compared against "now" including
// its seconds so the window can be tight.
bool inWindow(int nowSeconds, int targetSeconds) {
    return nowSeconds >= targetSeconds + kToleranceBeforeSec &&
           nowSeconds <= targetSeconds + kToleranceAfterSec;
}

[[noreturn]] void goToSleep(uint64_t timerFallbackSeconds, bool enableRtcWakeup) {
    WiFi.mode(WIFI_OFF);
    // Turn off LED before going to sleep
    digitalWrite(PIN_STATUS_LED, LOW);
    if (enableRtcWakeup) {
        const esp_sleep_ext1_wakeup_mode_t level_mode = ESP_EXT1_WAKEUP_ANY_LOW;
        esp_sleep_enable_ext1_wakeup(1ULL << PIN_RTC_SWQ, level_mode);
        // ESP32-C6 EXT1 takes a GPIO bitmask; DS3231 INT asserts LOW.
    } else {
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_EXT1);
    }
    esp_sleep_enable_timer_wakeup(timerFallbackSeconds * 1000000ULL);
    esp_deep_sleep_start();
    while (true) {
    }  // unreachable
}

}  // namespace

namespace Scheduler {

// Dusk2Dawn returns -1 for polar day/night, and doesn't wrap its result into
// [0, 1440) for extreme timezone/longitude combinations - normalize here so
// callers only ever see well-formed minute-of-day values.
//
// (year, month, day) is a UTC calendar date, and the result is UTC-native:
// passing timezone=0/isDST=false makes Dusk2Dawn return its raw UTC minutes
// (see Dusk2Dawn::sunriseSetUTC()) with no local/DST conversion at all -
// sunrise/sunset is purely a function of lat/lon/date, so no timezone is
// needed here once everything downstream works in UTC too.
SunTimes computeSunTimes(const Config& cfg, int year, int month, int day) {
    Dusk2Dawn location(cfg.lat, cfg.lon, /*timezone=*/0.0f);
    int sunrise = location.sunrise(year, month, day, /*isDST=*/false);
    int sunset = location.sunset(year, month, day, /*isDST=*/false);

    SunTimes result;
    result.valid = (sunrise != -1) && (sunset != -1);
    if (result.valid) {
        result.sunriseMinutes = normalizeMinutes(sunrise);
        result.sunsetMinutes = normalizeMinutes(sunset);
    }
    return result;
}

// Fixed-time schedules are stored in UTC minute-of-day (the RTC stores UTC,
// and all scheduling math compares against UTC) - local wall-clock values are
// only used in the web UI and converted to UTC at save time.
int resolveUtcMinutes(ScheduleMode mode, uint16_t absMinutes, int16_t sunOffsetMinutes,
                       int sunEventUtcMinutes, bool sunValid, const DateTime& utcDay,
                       const char* zoneName) {
    (void)utcDay;
    (void)zoneName;
    if (mode == ScheduleMode::SUN_OFFSET && sunValid) {
        return normalizeMinutes(sunEventUtcMinutes + sunOffsetMinutes);
    }
    return normalizeMinutes(absMinutes);
}

// The one place "open or close?" is dispatched for schedule resolution -
// handleDueActions()/armNextAlarmAndSleep() below and WebPortal's display
// code all go through this instead of duplicating the field picks.
int resolveScheduleMinutes(const Config& cfg, bool isOpen, const SunTimes& sun,
                            const DateTime& utcDay) {
    ScheduleMode mode = isOpen ? cfg.openMode : cfg.closeMode;
    uint16_t absMinutes = isOpen ? cfg.openAbsMinutes : cfg.closeAbsMinutes;
    int16_t sunOffsetMinutes = isOpen ? cfg.openSunOffsetMinutes : cfg.closeSunOffsetMinutes;
    int sunEventUtcMinutes = isOpen ? sun.sunriseMinutes : sun.sunsetMinutes;
    return resolveUtcMinutes(mode, absMinutes, sunOffsetMinutes, sunEventUtcMinutes, sun.valid,
                              utcDay, cfg.timezone);
}

void handleDueActions(Config& cfg, RtcManager& rtc, DoorController& door,
                      DoorAction requestedAction) {
    if (requestedAction == DoorAction::OPENED) {
        TRACE("[Scheduler] explicit open action");
        door.open(cfg);
        return;
    }
    if (requestedAction == DoorAction::CLOSED) {
        TRACE("[Scheduler] explicit close action");
        door.close(cfg);
        return;
    }

    if (!rtc.isTimeValid()) {
        // No valid time (never configured / lost power) - don't act on
        // garbage time. armNextAlarmAndSleep() will handle the short retry.
        return;
    }

    // Everything here runs in UTC - the DS3231 already stores it, and
    // resolveUtcMinutes() converts the (local) configured times into it
    // rather than the other way around.
    DateTime now = rtc.now();
    int nowMinutes = now.hour() * 60 + now.minute();
    int nowSeconds = nowMinutes * 60 + now.second();

    SunTimes sun = computeSunTimes(cfg, now.year(), now.month(), now.day());

    int openMinutes = resolveScheduleMinutes(cfg, /*isOpen=*/true, sun, now);
    int closeMinutes = resolveScheduleMinutes(cfg, /*isOpen=*/false, sun, now);

    // Scheduled actions use a tight time window. Explicit one-shot actions
    // bypass that window but still dispatch through this scheduler-owned path.
    bool openDue = inWindow(nowSeconds, openMinutes * 60);
    bool closeDue = inWindow(nowSeconds, closeMinutes * 60);

    // Basic debounce: compare against cfg.lastTriggerUnixTime - the last-
    // ACTED-ON trigger (shared between open and close; see ConfigStore.h's
    // comment on why one field is enough) - not the fire window itself, so
    // the "one or two consecutive polls" case above (or a reboot that lands
    // back in the same window) doesn't double-fire. The trigger is always
    // minute-quantized (seconds=0, see resolveUtcMinutes()), so comparing
    // it whole rather than flooring "now" to a minute is both simpler and
    // exact: identical polls against an unchanged schedule always resolve
    // to the identical trigger and get skipped, while a schedule that shifts
    // by even a minute, or tomorrow's occurrence of the same time-of-day,
    // resolves to a different value and fires normally. This is entirely
    // separate from cfg.lastOperationUnixTime - see DoorController.h - which
    // DoorController stamps itself with the real move time, not this value.
    uint32_t openTriggerUnix =
        DateTime(now.year(), now.month(), now.day(), openMinutes / 60, openMinutes % 60, 0)
            .unixtime();
    uint32_t closeTriggerUnix =
        DateTime(now.year(), now.month(), now.day(), closeMinutes / 60, closeMinutes % 60, 0)
            .unixtime();
    bool openIsNewTrigger = openTriggerUnix != cfg.lastTriggerUnixTime;
    bool closeIsNewTrigger = closeTriggerUnix != cfg.lastTriggerUnixTime;

    TRACEF("[Scheduler] now=%02d:%02d:%02d UTC | open=%02d:%02d UTC due=%d newTrigger=%d | "
           "close=%02d:%02d UTC due=%d newTrigger=%d",
           nowMinutes / 60, nowMinutes % 60, now.second(), openMinutes / 60, openMinutes % 60,
           openDue, openIsNewTrigger, closeMinutes / 60, closeMinutes % 60, closeDue,
           closeIsNewTrigger);

    // Sharing one field instead of one per action assumes open and close
    // never resolve to the same trigger; if they ever are configured to the
    // same time-of-day, whichever is checked first (open) wins and the
    // other is treated as "already done" - not a supported configuration.
    if (openDue && openIsNewTrigger) {
        cfg.lastTriggerUnixTime = openTriggerUnix;
        door.open(cfg);  // persists cfg, including the trigger line above
    }
    if (closeDue && closeIsNewTrigger) {
        cfg.lastTriggerUnixTime = closeTriggerUnix;
        door.close(cfg);  // persists cfg, including the trigger line above
    }
}

void armNextAlarmAndSleep(Config& cfg, RtcManager& rtc, ConfigStore& store) {
    if (!rtc.isTimeValid()) {
        // Without a valid RTC there is no meaningful scheduled event to arm.
        // Keep the session invariant by requesting an immediate WiFi service
        // wake so the user can set the clock.
        sleepForWifi(rtc, 1);
    }

    // Everything here runs in UTC - see handleDueActions().
    DateTime now = rtc.now();
    DateTime tomorrow = now + TimeSpan(1, 0, 0, 0);
    int nowSeconds = (now.hour() * 60 + now.minute()) * 60 + now.second();

    SunTimes sunToday = computeSunTimes(cfg, now.year(), now.month(), now.day());
    SunTimes sunTomorrow = computeSunTimes(cfg, tomorrow.year(), tomorrow.month(), tomorrow.day());

    int openTodayMin = resolveScheduleMinutes(cfg, /*isOpen=*/true, sunToday, now);
    int closeTodayMin = resolveScheduleMinutes(cfg, /*isOpen=*/false, sunToday, now);
    int openTomorrowMin = resolveScheduleMinutes(cfg, /*isOpen=*/true, sunTomorrow, tomorrow);

    // DS3231 Alarm1 (match hours/minutes/seconds, ignore date) always fires
    // at the *next* occurrence of the given time-of-day, so a "today" value
    // that has already passed simply rolls over to tomorrow in hardware.
    // That means a today's time-of-day already behind us can't be armed as
    // "today" - doing so would make the DS3231 roll it to tomorrow and skip
    // straight past a still-upcoming event later today (e.g. an 08:00 open
    // already past and an 18:00 close still ahead: naively arming the
    // earlier clock-time of the two, 08:00, would silently swallow today's
    // close). This is independent of whether either has already fired -
    // handleDueActions() has no memory of that, and neither does this.
    bool openUpcoming = nowSeconds <= openTodayMin * 60 + kToleranceAfterSec;
    bool closeUpcoming = nowSeconds <= closeTodayMin * 60 + kToleranceAfterSec;

    int nextMinute;
    AlarmOperateDoor nextOperation;
    DateTime nextEventDay;
    if (openUpcoming && closeUpcoming) {
        nextMinute = (openTodayMin < closeTodayMin) ? openTodayMin : closeTodayMin;
        nextOperation = (openTodayMin < closeTodayMin) ? AlarmOperateDoor::door_open
                                                        : AlarmOperateDoor::door_close;
        nextEventDay = now;
    } else if (openUpcoming) {
        nextMinute = openTodayMin;
        nextOperation = AlarmOperateDoor::door_open;
        nextEventDay = now;
    } else if (closeUpcoming) {
        nextMinute = closeTodayMin;
        nextOperation = AlarmOperateDoor::door_close;
        nextEventDay = now;
    } else {
        nextMinute = openTomorrowMin;
        nextOperation = AlarmOperateDoor::door_open;
        nextEventDay = tomorrow;
    }

    DateTime nextEventUtc(nextEventDay.year(), nextEventDay.month(), nextEventDay.day(),
                           nextMinute / 60, nextMinute % 60, 0);

    // Scheduled alarms carry the selected door operation and no WiFi startup;
    // wake at the actual target and handle the action directly in setup().
    DateTime wakeAt = nextEventUtc;

#ifdef DEBUG_TRACES
    // Runs on every boot right before going back to sleep, so this doubles
    // as the "what does the device think right now, and when will it next
    // wake up" boot trace.
    TimeZone::LocalTime nowLocal = TimeZone::toLocal(now, cfg.timezone);
    TimeZone::LocalTime nextEventLocal = TimeZone::toLocal(nextEventUtc, cfg.timezone);
    TRACEF("[Scheduler] RTC now: %04d-%02d-%02d %02d:%02d:%02d UTC / %04d-%02d-%02d %02d:%02d:%02d local",
           now.year(), now.month(), now.day(), now.hour(), now.minute(), now.second(),
           nowLocal.dt.year(), nowLocal.dt.month(), nowLocal.dt.day(), nowLocal.dt.hour(),
           nowLocal.dt.minute(), nowLocal.dt.second());
    TRACEF("[Scheduler] next event: %04d-%02d-%02d %02d:%02d:00 UTC / %04d-%02d-%02d %02d:%02d:00 "
           "local (openUpcoming=%d closeUpcoming=%d) | waking at %02d:%02d:%02d UTC",
           nextEventUtc.year(), nextEventUtc.month(), nextEventUtc.day(), nextEventUtc.hour(),
           nextEventUtc.minute(), nextEventLocal.dt.year(), nextEventLocal.dt.month(),
           nextEventLocal.dt.day(), nextEventLocal.dt.hour(), nextEventLocal.dt.minute(),
           openUpcoming, closeUpcoming, wakeAt.hour(), wakeAt.minute(), wakeAt.second());
#endif

    rtc.setNextAlarm(wakeAt, nextOperation, false);
    rtc.clearAlarm();

    goToSleep(kFallbackSleepSeconds, true);
}

[[noreturn]] void sleepForWifi(RtcManager& rtc, uint32_t seconds) {
    DateTime wakeAt = rtc.now() + TimeSpan(seconds);
    rtc.setNextAlarm(wakeAt, AlarmOperateDoor::no_door_operation, true);
    rtc.clearAlarm();
    goToSleep(kDebugSleepFallbackSeconds, true);
}

[[noreturn]] void sleepForDoorAction(RtcManager& rtc, uint32_t seconds,
                                     AlarmOperateDoor operation, bool wifiUp) {
    DateTime wakeAt = rtc.now() + TimeSpan(seconds);
    rtc.setNextAlarm(wakeAt, operation, wifiUp);
    rtc.clearAlarm();
    goToSleep(kDebugSleepFallbackSeconds, true);
}

}  // namespace Scheduler
