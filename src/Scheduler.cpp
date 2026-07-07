#include "Scheduler.h"

#include <Dusk2Dawn.h>
#include <WiFi.h>
#include <esp_sleep.h>

#include "Debug.h"
#include "config.h"  // must come before Arduino.h to override LED_BUILTIN
#include "TimeZone.h"

namespace {

// Wake window tolerance: deep-sleep wake isn't instant, and the fallback
// timer wake can land a while after the "real" alarm would have.
constexpr int kToleranceBeforeMin = -2;
constexpr int kToleranceAfterMin = 10;

// Safety net in case a DS3231 alarm is ever missed/misconfigured.
constexpr uint64_t kFallbackSleepSeconds = 6ULL * 3600ULL;
// RTC has no valid time yet - retry soon, but don't busy-loop.
constexpr uint64_t kInvalidTimeRetrySeconds = 600ULL;

int normalizeMinutes(int minutes) {
    minutes %= 1440;
    if (minutes < 0) minutes += 1440;
    return minutes;
}

bool inWindow(int nowMinutes, int targetMinutes) {
    return nowMinutes >= targetMinutes + kToleranceBeforeMin &&
           nowMinutes <= targetMinutes + kToleranceAfterMin;
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

void handleDueActions(Config& cfg, RtcManager& rtc, DoorController& door,
                       DueActionTracker* tracker) {
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

    SunTimes sun = computeSunTimes(cfg, now.year(), now.month(), now.day());

    int openMinutes = resolveUtcMinutes(cfg.openMode, cfg.openAbsMinutes, cfg.openSunOffsetMinutes,
                                         sun.sunriseMinutes, sun.valid, now, cfg.timezone);
    int closeMinutes = resolveUtcMinutes(cfg.closeMode, cfg.closeAbsMinutes,
                                          cfg.closeSunOffsetMinutes, sun.sunsetMinutes, sun.valid,
                                          now, cfg.timezone);

    // No "already done today" memory at all - purely "is now in the
    // trigger window". DoorController itself has no gate either (see its
    // comment), so this is the only thing standing between "it's time" and
    // the motor running. That means a wake that lands twice in the same
    // window (e.g. an overlapping fallback-timer wake) re-triggers the move
    // each time - deliberate, per the request to drop the day-based
    // bookkeeping entirely. The optional `tracker` is not that bookkeeping:
    // it's an in-RAM-only debounce keyed on the resolved target minute, used
    // by WebPortal's repeated same-session polling below (see
    // DueActionTracker's comment in Scheduler.h).
    bool openDue = inWindow(nowMinutes, openMinutes);
    bool closeDue = inWindow(nowMinutes, closeMinutes);

    bool openAlreadyFired = tracker != nullptr && tracker->lastOpenFiredMinutes == openMinutes;
    bool closeAlreadyFired = tracker != nullptr && tracker->lastCloseFiredMinutes == closeMinutes;

    TRACEF("[Scheduler] now=%02d:%02d UTC | open=%02d:%02d UTC due=%d skip=%d | "
           "close=%02d:%02d UTC due=%d skip=%d",
           nowMinutes / 60, nowMinutes % 60, openMinutes / 60, openMinutes % 60, openDue,
           openAlreadyFired, closeMinutes / 60, closeMinutes % 60, closeDue, closeAlreadyFired);

    if (openDue && !openAlreadyFired) {
        door.open(cfg);
        if (tracker != nullptr) tracker->lastOpenFiredMinutes = openMinutes;
    }
    if (closeDue && !closeAlreadyFired) {
        door.close(cfg);
        if (tracker != nullptr) tracker->lastCloseFiredMinutes = closeMinutes;
    }
}

void armNextAlarmAndSleep(Config& cfg, RtcManager& rtc, ConfigStore& store) {
    if (!rtc.isTimeValid()) {
        // Can't compute a real schedule yet - retry soon, no point arming ext0.
        goToSleep(kInvalidTimeRetrySeconds, /*armExt0=*/false);
    }

    // Everything here runs in UTC - see handleDueActions().
    DateTime now = rtc.now();
    DateTime tomorrow = now + TimeSpan(1, 0, 0, 0);
    int nowMinutes = now.hour() * 60 + now.minute();

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
    bool openUpcoming = nowMinutes <= openTodayMin + kToleranceAfterMin;
    bool closeUpcoming = nowMinutes <= closeTodayMin + kToleranceAfterMin;

    int nextMinute;
    if (openUpcoming && closeUpcoming) {
        nextMinute = (openTodayMin < closeTodayMin) ? openTodayMin : closeTodayMin;
    } else if (openUpcoming) {
        nextMinute = openTodayMin;
    } else if (closeUpcoming) {
        nextMinute = closeTodayMin;
    } else {
        nextMinute = openTomorrowMin;
    }

    uint8_t hh = static_cast<uint8_t>(nextMinute / 60);
    uint8_t mm = static_cast<uint8_t>(nextMinute % 60);

#ifdef DEBUG_TRACES
    // Runs on every boot right before going back to sleep, so this doubles
    // as the "what does the device think right now, and when will it next
    // wake up" boot trace.
    DateTime nextEventDay = (openUpcoming || closeUpcoming) ? now : tomorrow;
    DateTime nextEventUtc(nextEventDay.year(), nextEventDay.month(), nextEventDay.day(), hh, mm, 0);
    TimeZone::LocalTime nowLocal = TimeZone::toLocal(now, cfg.timezone);
    TimeZone::LocalTime nextEventLocal = TimeZone::toLocal(nextEventUtc, cfg.timezone);
    TRACEF("[Scheduler] RTC now: %04d-%02d-%02d %02d:%02d:%02d UTC / %04d-%02d-%02d %02d:%02d:%02d local",
           now.year(), now.month(), now.day(), now.hour(), now.minute(), now.second(),
           nowLocal.dt.year(), nowLocal.dt.month(), nowLocal.dt.day(), nowLocal.dt.hour(),
           nowLocal.dt.minute(), nowLocal.dt.second());
    TRACEF("[Scheduler] next wake: %04d-%02d-%02d %02d:%02d:00 UTC / %04d-%02d-%02d %02d:%02d:00 local "
           "(openUpcoming=%d closeUpcoming=%d)",
           nextEventUtc.year(), nextEventUtc.month(), nextEventUtc.day(), hh, mm,
           nextEventLocal.dt.year(), nextEventLocal.dt.month(), nextEventLocal.dt.day(),
           nextEventLocal.dt.hour(), nextEventLocal.dt.minute(), openUpcoming, closeUpcoming);
#endif

    rtc.setNextAlarm(hh, mm, 0);
    rtc.clearAlarm();

    goToSleep(kFallbackSleepSeconds, /*armExt0=*/true);
}

}  // namespace Scheduler
