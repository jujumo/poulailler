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
}

// Deep-sleep wake re-enters setup() from scratch, not loop() - all state
// lives in NVS (ConfigStore) and the DS3231 (RtcManager), nothing survives
// in RAM between cycles, so loop() is unused.
void setup() {
    Serial.begin(115200);

    // Blink blue LED to indicate the ESP32 is awake
    pinMode(PIN_STATUS_LED, OUTPUT);
    digitalWrite(PIN_STATUS_LED, HIGH);

    ConfigStore store;
    store.begin();
    Config cfg = store.load();

    RtcManager rtc;
    rtc.begin();

    DoorController door(store);
    door.begin();

    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

#ifdef DEBUG_TRACES
    // Printed immediately at boot, before the (potentially 5-minute-long)
    // config portal - so "what time does the device think it is" is visible
    // right away rather than only after armNextAlarmAndSleep() at the very
    // end of this same boot cycle.
    if (rtc.isTimeValid()) {
        DateTime utcNow = rtc.now();
        TimeZone::LocalTime localNow = TimeZone::toLocal(utcNow, cfg.timezone);
        TRACEF("[Boot] wake cause=%d RTC now: %04d-%02d-%02d %02d:%02d:%02d UTC / "
               "%04d-%02d-%02d %02d:%02d:%02d local",
               static_cast<int>(cause), utcNow.year(), utcNow.month(), utcNow.day(), utcNow.hour(),
               utcNow.minute(), utcNow.second(), localNow.dt.year(), localNow.dt.month(),
               localNow.dt.day(), localNow.dt.hour(), localNow.dt.minute(), localNow.dt.second());
    } else {
        TRACEF("[Boot] wake cause=%d, RTC time not valid (never set / lost power)",
               static_cast<int>(cause));
    }
#endif

    if (cause == ESP_SLEEP_WAKEUP_UNDEFINED) {
        // True power-on / reset / brownout: open the config portal.
        WebPortal portal(store, rtc, door);
        portal.run(kConfigPortalDurationMs);
        cfg = store.load();  // portal may have changed it
    } else {
        // Woken by the DS3231 alarm (ext0) or the fallback timer.
        rtc.clearAlarm();  // must happen before re-arming, see Scheduler
        Scheduler::handleDueActions(cfg, rtc, store, door);
    }


    Scheduler::armNextAlarmAndSleep(cfg, rtc, store);  // never returns
}

void loop() {}
