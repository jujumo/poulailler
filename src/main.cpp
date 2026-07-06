#include <Arduino.h>
#include <esp_sleep.h>

#include "config.h"
#include "ConfigStore.h"
#include "DoorController.h"
#include "RtcManager.h"
#include "Scheduler.h"
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

    // Keep status LED on for configured duration to indicate the ESP32 is awake
    delay(STATUS_LED_AWAKE_SECONDS * 1000UL);

    Scheduler::armNextAlarmAndSleep(cfg, rtc, store);  // never returns
}

void loop() {}
