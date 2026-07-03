#include "Scheduler.h"

#include <Dusk2Dawn.h>
#include <WiFi.h>
#include <esp_sleep.h>

#include "config.h"
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

uint16_t daysSinceEpoch(const DateTime& dt) {
    return static_cast<uint16_t>(dt.unixtime() / 86400UL);
}

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

void handleDueActions(Config& cfg, RtcManager& rtc, ConfigStore& store, DoorController& door) {
    if (!rtc.isTimeValid()) {
        // No valid time (never configured / lost power) - don't act on
        // garbage time. armNextAlarmAndSleep() will handle the short retry.
        return;
    }

    // Everything here runs in UTC - the DS3231 already stores it, and
    // resolveUtcMinutes() converts the (local) configured times into it
    // rather than the other way around.
    DateTime now = rtc.now();
    uint16_t today = daysSinceEpoch(now);
    int nowMinutes = now.hour() * 60 + now.minute();

    SunTimes sun = computeSunTimes(cfg, now.year(), now.month(), now.day());

    int openMinutes = resolveUtcMinutes(cfg.openMode, cfg.openAbsMinutes, cfg.openSunOffsetMinutes,
                                         sun.sunriseMinutes, sun.valid, now, cfg.timezone);
    int closeMinutes = resolveUtcMinutes(cfg.closeMode, cfg.closeAbsMinutes,
                                          cfg.closeSunOffsetMinutes, sun.sunsetMinutes, sun.valid,
                                          now, cfg.timezone);

    if (cfg.lastOpenDay != today && inWindow(nowMinutes, openMinutes)) {
        door.open(cfg);
        cfg.lastOpenDay = today;
        store.save(cfg);
    }
    if (cfg.lastCloseDay != today && inWindow(nowMinutes, closeMinutes)) {
        door.close(cfg);
        cfg.lastCloseDay = today;
        store.save(cfg);
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
    uint16_t today = daysSinceEpoch(now);

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

    bool openPending = (cfg.lastOpenDay != today);
    bool closePending = (cfg.lastCloseDay != today);

    // DS3231 Alarm1 (match hours/minutes/seconds, ignore date) always fires
    // at the *next* occurrence of the given time-of-day, so a "today" value
    // that has already passed simply rolls over to tomorrow in hardware -
    // no need to reason about which calendar day to arm for here. Each
    // candidate above was already resolved to UTC for its own specific
    // calendar day, so no further conversion is needed before arming.
    int nextMinute;
    if (openPending && closePending) {
        nextMinute = (openTodayMin < closeTodayMin) ? openTodayMin : closeTodayMin;
    } else if (openPending) {
        nextMinute = openTodayMin;
    } else if (closePending) {
        nextMinute = closeTodayMin;
    } else {
        nextMinute = openTomorrowMin;
    }

    uint8_t hh = static_cast<uint8_t>(nextMinute / 60);
    uint8_t mm = static_cast<uint8_t>(nextMinute % 60);
    rtc.setNextAlarm(hh, mm, 0);
    rtc.clearAlarm();

    goToSleep(kFallbackSleepSeconds, /*armExt0=*/true);
}

}  // namespace Scheduler
