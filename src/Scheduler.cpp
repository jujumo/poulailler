#include "Scheduler.h"

#include <WiFi.h>
#include <esp_sleep.h>

#include "PinConfig.h"
#include "SunCalc.h"

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

// Resolves a configured open/close schedule to a minute-of-day. Falls back
// to the absolute-time value if sun-offset mode is selected but sunrise/
// sunset could not be computed for this day (e.g. polar day/night).
int resolveMinutes(ScheduleMode mode, uint16_t absMinutes, int16_t sunOffsetMinutes,
                    int sunEventMinutes, bool sunValid) {
    if (mode == ScheduleMode::ABSOLUTE || !sunValid) {
        return absMinutes;
    }
    int minutes = (sunEventMinutes + sunOffsetMinutes) % 1440;
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

void handleDueActions(Config& cfg, RtcManager& rtc, ConfigStore& store, DoorController& door) {
    if (!rtc.isTimeValid()) {
        // No valid time (never configured / lost power) - don't act on
        // garbage time. armNextAlarmAndSleep() will handle the short retry.
        return;
    }

    DateTime now = rtc.now();
    uint16_t today = daysSinceEpoch(now);
    int nowMinutes = now.hour() * 60 + now.minute();

    SunTimes sun = calculateSunTimes(cfg.lat, cfg.lon, now.year(), now.month(), now.day(),
                                      cfg.utcOffsetMinutes);

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

    DateTime now = rtc.now();
    DateTime tomorrow = now + TimeSpan(1, 0, 0, 0);
    uint16_t today = daysSinceEpoch(now);

    SunTimes sunToday = calculateSunTimes(cfg.lat, cfg.lon, now.year(), now.month(), now.day(),
                                           cfg.utcOffsetMinutes);
    SunTimes sunTomorrow = calculateSunTimes(cfg.lat, cfg.lon, tomorrow.year(), tomorrow.month(),
                                              tomorrow.day(), cfg.utcOffsetMinutes);

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
    // no need to reason about which calendar day to arm for here.
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
