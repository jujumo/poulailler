#include <Arduino.h>

#include "Config.h"
#include "Debug.h"
#include "DoorController.h"
#include "Scheduler.h"
#include "SleepManager.h"
#include "TimeTools.h"
#include "WebPortal.h"

namespace {

constexpr unsigned long kConfigPortalDurationMs = 5UL * 60UL * 1000UL;


void processPortalRequest(
    Config& config,
    SleepManager& sleep_manager,
    Scheduler& scheduler,
    DoorController& door)
{
    WebPortal portal(config, sleep_manager, scheduler, door);

    TRACE("[Main] before portal.run");
    portal.run(kConfigPortalDurationMs);
    TRACE("[Main] after portal.run");
    if (!sleep_manager.isTimeValid())
    {
        TRACE("[Main::web] ERROR: RTC invalid. no request handled.");
        return;
    }
    ConfigStore::print(config);
    Serial.flush();
    sleep(2);

    TRACE("[Main::web] done.");
    TRACE(scheduler_to_string(scheduler).c_str());
}
} // namespace

void setup()
{
    Serial.begin(115200);

    const unsigned long serialWaitStart = millis();
    while (!Serial && millis() - serialWaitStart < 2000UL) {
        delay(10);
    }

    TRACE("[Boot] Bonjour from Poulailler!");
    pinMode(PIN_STATUS_LED, OUTPUT);
    digitalWrite(PIN_STATUS_LED, HIGH);

    Config config = ConfigStore::load();
    TRACE("[Boot] config loaded!");
    ConfigStore::print(config);

    SleepManager sleep_manager;
    if (!sleep_manager.begin()) {
        TRACE("[Boot] ERROR: SleepManager initialization failed.");
        digitalWrite(PIN_STATUS_LED, LOW);
        while (true) {delay(1000);}
    }
    TRACE("[Boot] sleep_manager loaded!");

    Scheduler scheduler;
    if (!scheduler.load()) {
        TRACE("[Boot] ERROR: scheduler load failed.");
    }
    TRACE(scheduler_to_string(scheduler).c_str());;
    TRACE("[Boot] scheduler loaded!");

    DoorController door(config);
    door.begin();
    TRACE("[Boot] door loaded!");
    
    const SleepManager::WakeCause wake_cause = sleep_manager.wakeCause();
    TRACEF("[Boot] wake_cause enum=%d", static_cast<int>(wake_cause));
    TRACEF("[Boot] wake cause=%s", wakeCauseName(wake_cause));
    const bool rtc_valid = sleep_manager.isTimeValid();
    if (!rtc_valid) {
        TRACE("[Boot] ERROR: RTC time is invalid.");
        TRACE("[Boot] ERROR: Unpredictable behavior.");
    }
    const DateTime now = rtc_valid ? sleep_manager.now() : DateTime(2000, 1, 1, 0, 0, 0);
    TRACEF("[Boot] RTC=%s UTC", TimeTools::convert_time_to_string(now).c_str());

    /*
     * Unified wake dispatcher.
     *
     * POWER_ON always starts the WiFi portal.
     * Otherwise, the first scheduled action is considered only when due.
     */


    Scheduler::Action due_action;
    TRACE(scheduler_to_string(scheduler).c_str());

    if ( 
        wake_cause == SleepManager::WakeCause::POWER_ON 
        ||
        wake_cause == SleepManager::WakeCause::OTHER
        ||
        !rtc_valid
    )
    {
        TRACE("[main] power-on/reset/error: Override due action");
        scheduler.clear(); // lets start on clean slate (in case of power-on/reset)
        ConfigStore::clear();
        due_action = Scheduler::Action(now, Scheduler::ActionType::WifiService);
    } 
    else 
    {
        TRACE("[main] poping due action");
        due_action = scheduler.popFirstDueAction(now);
    }
    
    const bool wifi_service_due =
        due_action.type == Scheduler::ActionType::WifiService;

    const bool door_action_due =
        due_action.type == Scheduler::ActionType::DoorOpen
        ||
        due_action.type == Scheduler::ActionType::DoorClose;

    TRACEF("[main] retained due action: %s.", action_to_string(due_action).c_str());
    TRACEF("[main] must wifi %d", wifi_service_due);
    TRACEF("[main] must door %d", door_action_due);
    if (wifi_service_due) 
    {
        TRACE("[main] due WifiService: starting WiFi portal");
        door.jitter();
        processPortalRequest(config, sleep_manager, scheduler, door);
    }
    else if (door_action_due) 
    {
        TRACEF( "[Main] executing action=%s @ %lu",
            actionTypeName(due_action.type),
            static_cast<unsigned long>(due_action.timestamp.unixtime())
        );

        switch (due_action.type) {
            case Scheduler::ActionType::DoorOpen:
                TRACE("[Main] executing DoorOpen");
                door.open();
                break;

            case Scheduler::ActionType::DoorClose:
                TRACE("[Main] executing DoorClose");
                door.close();
                break;

            case Scheduler::ActionType::WifiService:
                // Not expected here: only due door actions enter this branch.
                TRACE("[Main] unexpected WifiService in door branch");
                break;
        }
    }
    
    // Keep at least two future regular door actions scheduled.
    if (rtc_valid) 
    {
        const DateTime schedule_now = sleep_manager.now();
        TRACE("[Main] updating schedule after wake");
        if (!scheduler.update_schedule(config, schedule_now)) {
            TRACE("[Main] ERROR: schedule update after wake failed.");
        }
    }

    scheduler.save();
    
    // Arm the first scheduled action and go back to deep sleep.
    TRACE("[Main] before scheduler.nextAction (final)");
    const Scheduler::Action* next = scheduler.nextAction();
    TRACEF("[Main] nextAction ptr (final)=%p",
        static_cast<void*>(const_cast<Scheduler::Action*>(next))
    );
    ConfigStore::print(config);
    TRACE(scheduler_to_string(scheduler).c_str());

    if (next != nullptr) {
        TRACEF("[Main] before final sleepUntil %s", 
            TimeTools::convert_time_to_string(next->timestamp).c_str());
        Serial.flush();
        sleep(1);
        sleep_manager.sleepUntil(next->timestamp);
    }

    TRACE("[Main] ERROR: after sleepUntil : SHOULD NEVER SEE THIS !");
}

void loop()
{
    // Intentionally empty.
}