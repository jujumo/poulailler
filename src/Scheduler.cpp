#include "Scheduler.h"

#include <Dusk2Dawn.h>
#include <WiFi.h>
#include <esp_sleep.h>

#include "PinConfig.h"
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

// Resolves a configured open/close schedule to a minute-of-day. Falls back
// to the absolute-time value if sun-offset mode is selected but sunrise/
// sunset could not be computed for this day (e.g. polar day/night).
int resolveMinutes(ScheduleMode mode, uint16_t absMinutes, int16_t sunOffsetMinutes,
                    int sunEventMinutes, bool sunValid) {
    if (mode == ScheduleMode::ABSOLUTE || !sunValid) {
        return absMinutes;
    }
    return normalizeMinutes(sunEventMinutes + sunOffsetMinutes);
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
SunTimes computeSunTimes(const Config& cfg, int year, int month, int day) {
    // Dusk2Dawn's `timezone` ctor arg is a plain UTC offset; folding this
    // date's DST-aware offset straight into it and always passing
    // isDST=false is equivalent to (and simpler than) passing the *standard*
    // offset separately with isDST=true, since Dusk2Dawn::sunriseSet() just
    // adds the two together anyway. Note it truncates this to whole hours,
    // so half-hour-offset zones (e.g. Asia/Kolkata) lose their :30 here -
    // a limitation of the library, not of this offset calculation.
    float timezoneHours =
        TimeZone::utcOffsetMinutesForLocalDate(year, month, day, cfg.timezone) / 60.0f;
    Dusk2Dawn location(cfg.lat, cfg.lon, timezoneHours);
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

void handleDueActions(Config& cfg, RtcManager& rtc, ConfigStore& store, DoorController& door) {
    if (!rtc.isTimeValid()) {
        // No valid time (never configured / lost power) - don't act on
        // garbage time. armNextAlarmAndSleep() will handle the short retry.
        return;
    }

    // The DS3231 stores UTC; open/close times are configured in local wall
    // clock, so resolve "now" to local before comparing against them.
    DateTime now = TimeZone::toLocal(rtc.now(), cfg.timezone).dt;
    uint16_t today = daysSinceEpoch(now);
    int nowMinutes = now.hour() * 60 + now.minute();

    SunTimes sun = computeSunTimes(cfg, now.year(), now.month(), now.day());

    int openMinutes = resolveMinutes(cfg.openMode, cfg.openAbsMinutes, cfg.openSunOffsetMinutes,
                                      sun.sunriseMinutes, sun.valid);
    int closeMinutes = resolveMinutes(cfg.closeMode, cfg.closeAbsMinutes, cfg.closeSunOffsetMinutes,
                                       sun.sunsetMinutes, sun.valid);

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

    // The DS3231 stores UTC; open/close times are configured in local wall
    // clock, so resolve "now" to local before comparing against them.
    DateTime now = TimeZone::toLocal(rtc.now(), cfg.timezone).dt;
    DateTime tomorrow = now + TimeSpan(1, 0, 0, 0);
    uint16_t today = daysSinceEpoch(now);

    SunTimes sunToday = computeSunTimes(cfg, now.year(), now.month(), now.day());
    SunTimes sunTomorrow = computeSunTimes(cfg, tomorrow.year(), tomorrow.month(), tomorrow.day());

    int openTodayMin = resolveMinutes(cfg.openMode, cfg.openAbsMinutes, cfg.openSunOffsetMinutes,
                                       sunToday.sunriseMinutes, sunToday.valid);
    int closeTodayMin = resolveMinutes(cfg.closeMode, cfg.closeAbsMinutes, cfg.closeSunOffsetMinutes,
                                        sunToday.sunsetMinutes, sunToday.valid);
    int openTomorrowMin = resolveMinutes(cfg.openMode, cfg.openAbsMinutes, cfg.openSunOffsetMinutes,
                                          sunTomorrow.sunriseMinutes, sunTomorrow.valid);

    bool openPending = (cfg.lastOpenDay != today);
    bool closePending = (cfg.lastCloseDay != today);

    // DS3231 Alarm1 (match hours/minutes/seconds, ignore date) always fires
    // at the *next* occurrence of the given time-of-day, so a "today" value
    // that has already passed simply rolls over to tomorrow in hardware -
    // no need to reason about which calendar day to arm for here. We do
    // still need to know *which* calendar day the chosen local minute
    // belongs to, though, since converting it to the UTC time-of-day the
    // alarm actually runs on depends on whether that specific day is inside
    // a DST period.
    int nextMinute;
    DateTime targetDay = now;
    if (openPending && closePending) {
        nextMinute = (openTodayMin < closeTodayMin) ? openTodayMin : closeTodayMin;
    } else if (openPending) {
        nextMinute = openTodayMin;
    } else if (closePending) {
        nextMinute = closeTodayMin;
    } else {
        nextMinute = openTomorrowMin;
        targetDay = tomorrow;
    }

    DateTime targetLocal(targetDay.year(), targetDay.month(), targetDay.day(), nextMinute / 60,
                          nextMinute % 60, 0);
    DateTime targetUtc = TimeZone::toUtc(targetLocal, cfg.timezone);
    rtc.setNextAlarm(targetUtc.hour(), targetUtc.minute(), targetUtc.second());
    rtc.clearAlarm();

    goToSleep(kFallbackSleepSeconds, /*armExt0=*/true);
}

}  // namespace Scheduler
