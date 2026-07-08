#include "Scheduler.h"

#include <Dusk2Dawn.h>
#include <WiFi.h>
#include <esp_sleep.h>

#include "Debug.h"
#include "config.h"  // must come before Arduino.h to override LED_BUILTIN
#include "TimeZone.h"

namespace {

// How early (before a computed open/close target) armNextAlarmAndSleep()
// wakes the device via the DS3231 alarm, to absorb ESP32 boot/WiFi-bringup
// latency - by the time the target actually arrives, WebPortal's poll loop
// has already been running for a while, so handleDueActions() only needs a
// tight window (below) rather than a wide post-hoc tolerance. Unrelated to
// the multi-hour fallback timer wake, which stays a coarse safety net.
constexpr int kWakeLeadMinutes = 2;

// Fire tolerance for handleDueActions(), in seconds either side of the
// target - a little wider than WebPortal's poll interval
// (kScheduleCheckIntervalMs, 5s) so a target is never polled-past without
// being caught, now that kWakeLeadMinutes above (not this) absorbs boot
// latency.
constexpr int kToleranceBeforeSec = -5;
constexpr int kToleranceAfterSec = 5;

// Safety net in case a DS3231 alarm is ever missed/misconfigured.
constexpr uint64_t kFallbackSleepSeconds = 6ULL * 3600ULL;
// RTC has no valid time yet - retry soon, but don't busy-loop.
constexpr uint64_t kInvalidTimeRetrySeconds = 600ULL;

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

[[noreturn]] void goToSleep(uint64_t timerFallbackSeconds, bool armExt0) {
    WiFi.mode(WIFI_OFF);
    // Turn off LED before going to sleep
    digitalWrite(PIN_STATUS_LED, LOW);
    if (armExt0) {
        esp_sleep_enable_ext0_wakeup(PIN_RTC_INT, 0);  // DS3231 INT asserts LOW
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

// Converts a configured LOCAL absolute time-of-day (as entered in the web
// UI) to the UTC minute-of-day it corresponds to on the given UTC calendar
// day. Computed fresh on every call rather than cached/stored, so a DST
// transition between now and the target is picked up automatically - no
// manual re-save needed twice a year.
int localAbsMinutesToUtc(uint16_t localMinutes, const DateTime& utcDay, const char* zoneName) {
    DateTime localTarget(utcDay.year(), utcDay.month(), utcDay.day(), localMinutes / 60,
                          localMinutes % 60, 0);
    DateTime utcTarget = TimeZone::toUtc(localTarget, zoneName);
    return utcTarget.hour() * 60 + utcTarget.minute();
}

// Resolves a configured open/close schedule to a UTC minute-of-day for the
// given UTC calendar day. Sun-offset mode is already UTC-native (see
// computeSunTimes()) so it needs no conversion; absolute mode is
// user-entered local time and must be resolved per calendar day via
// localAbsMinutesToUtc() above. Falls back to absolute if sun-offset mode
// is selected but sunrise/sunset could not be computed (e.g. polar
// day/night). Exposed (not file-local) so WebPortal can show the user the
// same resolved time it's actually scheduled against.
int resolveUtcMinutes(ScheduleMode mode, uint16_t absMinutes, int16_t sunOffsetMinutes,
                       int sunEventUtcMinutes, bool sunValid, const DateTime& utcDay,
                       const char* zoneName) {
    if (mode == ScheduleMode::SUN_OFFSET && sunValid) {
        return normalizeMinutes(sunEventUtcMinutes + sunOffsetMinutes);
    }
    return localAbsMinutesToUtc(absMinutes, utcDay, zoneName);
}

void handleDueActions(Config& cfg, RtcManager& rtc, DoorController& door) {
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

    int openMinutes = resolveUtcMinutes(cfg.openMode, cfg.openAbsMinutes, cfg.openSunOffsetMinutes,
                                         sun.sunriseMinutes, sun.valid, now, cfg.timezone);
    int closeMinutes = resolveUtcMinutes(cfg.closeMode, cfg.closeAbsMinutes,
                                          cfg.closeSunOffsetMinutes, sun.sunsetMinutes, sun.valid,
                                          now, cfg.timezone);

    // No "already done" memory at all - purely "is now within a few
    // seconds of the trigger target". DoorController itself has no gate
    // either (see its comment), so this is the only thing standing between
    // "it's time" and the motor running. Safe to keep this simple because
    // armNextAlarmAndSleep() wakes the device kWakeLeadMinutes before the
    // target and WebPortal polls this every few seconds the whole time it's
    // awake - by the time "now" actually lands in this tight window, it'll
    // typically only do so for one or two consecutive polls before moving
    // past it, rather than lingering there for many minutes.
    bool openDue = inWindow(nowSeconds, openMinutes * 60);
    bool closeDue = inWindow(nowSeconds, closeMinutes * 60);

    TRACEF("[Scheduler] now=%02d:%02d:%02d UTC | open=%02d:%02d UTC due=%d | "
           "close=%02d:%02d UTC due=%d",
           nowMinutes / 60, nowMinutes % 60, now.second(), openMinutes / 60, openMinutes % 60,
           openDue, closeMinutes / 60, closeMinutes % 60, closeDue);

    if (openDue) door.open(cfg);
    if (closeDue) door.close(cfg);
}

void armNextAlarmAndSleep(Config& cfg, RtcManager& rtc, ConfigStore& store) {
    if (!rtc.isTimeValid()) {
        // Can't compute a real schedule yet - retry soon, no point arming ext0.
        goToSleep(kInvalidTimeRetrySeconds, /*armExt0=*/false);
    }

    // Everything here runs in UTC - see handleDueActions().
    DateTime now = rtc.now();
    DateTime tomorrow = now + TimeSpan(1, 0, 0, 0);
    int nowSeconds = (now.hour() * 60 + now.minute()) * 60 + now.second();

    SunTimes sunToday = computeSunTimes(cfg, now.year(), now.month(), now.day());
    SunTimes sunTomorrow = computeSunTimes(cfg, tomorrow.year(), tomorrow.month(), tomorrow.day());

    int openTodayMin = resolveUtcMinutes(cfg.openMode, cfg.openAbsMinutes, cfg.openSunOffsetMinutes,
                                          sunToday.sunriseMinutes, sunToday.valid, now, cfg.timezone);
    int closeTodayMin = resolveUtcMinutes(cfg.closeMode, cfg.closeAbsMinutes,
                                           cfg.closeSunOffsetMinutes, sunToday.sunsetMinutes,
                                           sunToday.valid, now, cfg.timezone);
    int openTomorrowMin =
        resolveUtcMinutes(cfg.openMode, cfg.openAbsMinutes, cfg.openSunOffsetMinutes,
                           sunTomorrow.sunriseMinutes, sunTomorrow.valid, tomorrow, cfg.timezone);

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
    DateTime nextEventDay;
    if (openUpcoming && closeUpcoming) {
        nextMinute = (openTodayMin < closeTodayMin) ? openTodayMin : closeTodayMin;
        nextEventDay = now;
    } else if (openUpcoming) {
        nextMinute = openTodayMin;
        nextEventDay = now;
    } else if (closeUpcoming) {
        nextMinute = closeTodayMin;
        nextEventDay = now;
    } else {
        nextMinute = openTomorrowMin;
        nextEventDay = tomorrow;
    }

    DateTime nextEventUtc(nextEventDay.year(), nextEventDay.month(), nextEventDay.day(),
                           nextMinute / 60, nextMinute % 60, 0);

    // Wake kWakeLeadMinutes before the actual target rather than at the
    // target itself, so ESP32 boot/WiFi-bringup latency happens before the
    // target arrives instead of eating into handleDueActions()'s tight fire
    // window (see its comment). Clamped to never be earlier than "now" -
    // an alarm time in the past would make the DS3231's ignore-date match
    // roll over to the *next* occurrence a full day later instead of firing
    // shortly, silently missing today's event; this only bites when the
    // target itself is already less than kWakeLeadMinutes away, in which
    // case there's nothing to lead-in for anyway.
    DateTime wakeAt = nextEventUtc - TimeSpan(60 * kWakeLeadMinutes);
    if (wakeAt < now) wakeAt = now + TimeSpan(1);

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

    rtc.setNextAlarm(wakeAt.hour(), wakeAt.minute(), wakeAt.second());
    rtc.clearAlarm();

    goToSleep(kFallbackSleepSeconds, /*armExt0=*/true);
}

}  // namespace Scheduler
