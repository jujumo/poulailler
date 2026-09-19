#include "Scheduler.h"

#include <WiFi.h>
#include <esp_sleep.h>

#include "Debug.h"
#include "build_config.h"  // must come before Arduino.h to override LED_BUILTIN


namespace {

// Scheduler selection tolerance: keep an event eligible for a few seconds
// after its target while the boot that should consume it is starting.
constexpr int kToleranceAfterSec = 5;

// Scheduled wake validation tolerance. This is deliberately asymmetric:
// early wakes must wait for the RTC to reach the requested target.
constexpr int kScheduledAlarmToleranceAfterSec = 120;

// Safety net in case a DS3231 alarm is ever missed/misconfigured.
constexpr uint64_t kFallbackSleepSeconds = 24ULL * 3600ULL;
constexpr uint64_t kDebugSleepFallbackSeconds = 60ULL;

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



bool scheduledAlarmInWindow(int64_t offsetSeconds) {
    return offsetSeconds >= 0 && offsetSeconds <= kScheduledAlarmToleranceAfterSec;
}

void armNextAlarmAndSleep(Config& cfg, RtcManager& rtc, ConfigStore& store) {
    if (!rtc.isTimeValid()) {
        // Without a valid RTC there is no meaningful scheduled event to arm.
        // Keep the session invariant by requesting an immediate WiFi service
        // wake so the user can set the clock.
        sleepForWifi(rtc, 1);
    }

    // Everything here runs in UTC - see decideDoorAction().
    DateTime now = rtc.now();
    DateTime tomorrow = now + TimeSpan(1, 0, 0, 0);
    int nowSeconds = (now.hour() * 60 + now.minute()) * 60 + now.second();

//    rtc.setNextAlarm(wakeAt, nextOperation, false);
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
