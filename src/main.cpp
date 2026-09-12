#include <Arduino.h>
#include <esp_sleep.h>

#include "config.h"
#include "ConfigStore.h"
#include "Debug.h"
#include "DoorController.h"
#include "RtcManager.h"
#include "Scheduler.h"
#include "TimeZone.h"
#include "WebPortal.h"

namespace {
constexpr unsigned long kConfigPortalDurationMs = 5UL * 60UL * 1000UL;

#ifdef DEBUG_TRACES
// UNDEFINED here means a *real* reset (power-on, manual reset, or a
// brownout) rather than a clean deep-sleep wake - worth naming explicitly
// since a brownout mid-move (the motor's current draw sagging the supply)
// reroutes this boot into the 5-minute config portal instead of resuming
// the schedule, and cfg.lastOperationAction won't have advanced past
// whatever completed before the interrupted move.
const char* wakeCauseName(esp_sleep_wakeup_cause_t cause) {
    switch (cause) {
        case ESP_SLEEP_WAKEUP_UNDEFINED:
            return "UNDEFINED (power-on/reset/brownout)";
        case ESP_SLEEP_WAKEUP_EXT0:
            return "EXT0 (RTC alarm)";
        case ESP_SLEEP_WAKEUP_TIMER:
            return "TIMER (fallback safety net)";
        default:
            return "OTHER";
    }
}

const char* doorActionName(DoorAction action) {
    switch (action) {
        case DoorAction::OPENED:
            return "OPENED";
        case DoorAction::CLOSED:
            return "CLOSED";
        default:
            return "NONE";
    }
}
#endif
}

// Deep-sleep wake re-enters setup() from scratch, not loop() - all state
// lives in NVS (ConfigStore) and the DS3231 (RtcManager), nothing survives
// in RAM between cycles, so loop() is unused.
void setup() {
    Serial.begin(115200);
    unsigned long serialWaitStart = millis();
    while (!Serial && millis() - serialWaitStart < 2000UL) {
        delay(10);
    }
    Serial.println("Hello from Poulailler!"); 

    // Blink blue LED to indicate the ESP32 is awake
    pinMode(PIN_STATUS_LED, OUTPUT);
    digitalWrite(PIN_STATUS_LED, HIGH);

    ConfigStore store;
    store.begin();
    Config cfg = store.load();

    RtcManager rtc;
    rtc.begin();

    DoorController door(store, rtc);
    door.begin();

    // Every wake - true reset, DS3231 alarm, or the fallback timer - takes
    // the same path below (open the portal, then re-arm); `cause` only
    // matters for tracing which one this was.
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    (void)cause;

#ifdef DEBUG_TRACES
    // Printed immediately at boot, before the (potentially 5-minute-long)
    // config portal - so "what time does the device think it is" is visible
    // right away rather than only after armNextAlarmAndSleep() at the very
    // end of this same boot cycle.
    if (rtc.isTimeValid()) {
        DateTime utcNow = rtc.now();
        TimeZone::LocalTime localNow = TimeZone::toLocal(utcNow, cfg.timezone);
        TRACEF("[Boot] wake cause=%s lastEvent=%s@%lu RTC now: %04d-%02d-%02d %02d:%02d:%02d UTC / "
               "%04d-%02d-%02d %02d:%02d:%02d local",
               wakeCauseName(cause), doorActionName(cfg.lastOperationAction),
               static_cast<unsigned long>(cfg.lastOperationUnixTime), utcNow.year(), utcNow.month(),
               utcNow.day(), utcNow.hour(), utcNow.minute(), utcNow.second(), localNow.dt.year(),
               localNow.dt.month(), localNow.dt.day(), localNow.dt.hour(), localNow.dt.minute(),
               localNow.dt.second());
    } else {
        TRACEF("[Boot] wake cause=%s lastEvent=%s@%lu, RTC time not valid (never set / lost power)",
               wakeCauseName(cause), doorActionName(cfg.lastOperationAction),
               static_cast<unsigned long>(cfg.lastOperationUnixTime));
    }
#endif

    // Clear the alarm-fired flag right away regardless of wake cause - see
    // the correctness rule in CLAUDE.md; must happen again immediately
    // before every esp_deep_sleep_start() too (see Scheduler).
    rtc.clearAlarm();

    // There's no separate "just run the scheduler and go back to sleep"
    // path: armNextAlarmAndSleep() now wakes the device kWakeLeadMinutes
    // before an open/close target rather than at it (see Scheduler.cpp), so
    // the portal's own periodic handleDueActions() poll (WebPortal::run())
    // is what actually catches the target precisely, and running the
    // portal on every wake means it's reachable without a manual reset.
    WebPortal portal(store, rtc, door);
    portal.run(kConfigPortalDurationMs);
    cfg = store.load();  // portal may have changed it

    Scheduler::armNextAlarmAndSleep(cfg, rtc, store);  // never returns
}

void loop() {}
