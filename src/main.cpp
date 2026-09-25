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

#ifdef DEBUG_TRACES

const char* wakeCauseName(SleepManager::WakeCause cause)
{
    switch (cause) {
        case SleepManager::WakeCause::POWER_ON:
            return "POWER_ON";
        case SleepManager::WakeCause::RTC_ALARM:
            return "RTC_ALARM";
        default:
            return "OTHER";
    }
}

const char* actionTypeName(Scheduler::ActionType type)
{
    switch (type) {
        case Scheduler::ActionType::DoorOpen:
            return "DoorOpen";
        case Scheduler::ActionType::DoorClose:
            return "DoorClose";
        case Scheduler::ActionType::WifiService:
            return "WifiService";
        default:
            return "UNKNOWN";
    }
}

const char* doorActionName(DoorAction action)
{
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


WebPortalRequest runPortal(
    Config& config,
    SleepManager& sleep_manager)
{
    WebPortal portal(config, sleep_manager);

    TRACE("[Main] before portal.run");
    const WebPortalRequest request =
        portal.run(kConfigPortalDurationMs);
    TRACE("[Main] after portal.run");
    TRACEF("[Main] request=%d", static_cast<int>(request));

    TRACE("[Main] before ConfigStore::load after portal");
    config = ConfigStore::load();
    TRACE("[Main] after ConfigStore::load after portal");

    return request;
}

void processPortalRequest(
    Config& config,
    SleepManager& sleep_manager,
    Scheduler& scheduler)
{
    const WebPortalRequest request = runPortal(config, sleep_manager);
    if (!sleep_manager.isTimeValid()) 
    {
        TRACE("[Main::web] ERROR: RTC invalid. no request handled.");
        return;
    }
    TRACEF("[Main::web] after run request = %d", request);
    ConfigStore::print(config);
    Serial.flush();
    sleep(2);

    const DateTime now = sleep_manager.now();
    //// CONFIG CHANGED /////////////////////////////////////////////////////
    // nothing 
    
    //// FORCE DOOR ////////////////////////////////////////////////////////////
    if (request == WebPortalRequest::FORCE_OPEN ||
        request == WebPortalRequest::FORCE_CLOSE) 
    {
        TRACE("[Main::web] Force door action requested.");
        Scheduler::Action doorAction;
        doorAction.timestamp = now;
        doorAction.type = request == WebPortalRequest::FORCE_OPEN
                            ? Scheduler::ActionType::DoorOpen
                            : Scheduler::ActionType::DoorClose;

        scheduler.addAction(doorAction);
        TRACE("[Main::web] door action added.");

        Scheduler::Action wifiAction;
        wifiAction.timestamp = now + TimeSpan(0, 0, 0, 2);
        wifiAction.type = Scheduler::ActionType::WifiService;
        scheduler.addAction(wifiAction);
        TRACE("[Main::web] WiFi action added");
    }
    
    //// NAP //////////////////////////////////////////////////////////////////////
    if (request == WebPortalRequest::NAP) 
    {
        TRACE("[Main::web] Nap requested.");
        Scheduler::Action wifiAction;
        wifiAction.timestamp = now+ TimeSpan(0, 0, 0, 10);
        wifiAction.type = Scheduler::ActionType::WifiService;
        scheduler.addAction(wifiAction);
        TRACE("[Main::web] WiFi action added");
    }

    TRACE("[Main::web] done.");
    scheduler.print();
}
} // namespace

void setup()
{
    Serial.begin(115200);

    const unsigned long serialWaitStart = millis();
    while (!Serial && millis() - serialWaitStart < 2000UL) {
        delay(10);
    }
#   ifdef DEBUG_TRACES
    Serial.println("[Boot] Bonjour from Poulailler!");
#   endif
    pinMode(PIN_STATUS_LED, OUTPUT);
    digitalWrite(PIN_STATUS_LED, HIGH);

    Config config = ConfigStore::load();
    ConfigStore::print(config);
    TRACE("[Boot] config loaded!");

    SleepManager sleep_manager;
    if (!sleep_manager.begin()) {
        TRACE("[Boot] SleepManager initialization failed");
        digitalWrite(PIN_STATUS_LED, LOW);
        while (true) {
            delay(1000);
        }
    }
    TRACE("[Boot] sleep_manager loaded!");

    Scheduler scheduler;
    if (!scheduler.load()) {
        TRACE("[Boot] scheduler load failed");
    }
    scheduler.print();
    TRACE("[Boot] scheduler loaded!");

    DoorController door(config);
    door.begin();
    TRACE("[Boot] door loaded!");
    
    const SleepManager::WakeCause wake_cause = sleep_manager.wakeCause();
    TRACEF("[Boot] wake_cause enum=%d", static_cast<int>(wake_cause));

    TRACEF("[Boot] wake cause=%s", wakeCauseName(wake_cause));
    if (sleep_manager.isTimeValid()) {
        const DateTime now = sleep_manager.now();
        TRACEF("[Boot] RTC=%s UTC", TimeTools::convert_time_to_string(now).c_str());
    }
    else {
        TRACE("[Boot] RTC time is invalid");
    }


    /*
     * Unified wake dispatcher.
     *
     * POWER_ON always starts the WiFi portal.
     * Otherwise, the first scheduled action is considered only when due.
     */
    const bool rtc_valid = sleep_manager.isTimeValid();
    const DateTime now = rtc_valid ? sleep_manager.now() : DateTime(2000, 1, 1, 0, 0, 0);
    const Scheduler::Action* first_action = scheduler.nextAction();

    const bool first_action_due =
        rtc_valid &&
        first_action != nullptr &&
        first_action->timestamp <= now;

    const bool wifi_service_due =
        first_action_due &&
        first_action->type == Scheduler::ActionType::WifiService;

    const bool door_action_due =
        first_action_due &&
        (first_action->type == Scheduler::ActionType::DoorOpen ||
         first_action->type == Scheduler::ActionType::DoorClose);

    if (wake_cause == SleepManager::WakeCause::POWER_ON )
    {
        TRACE("[main] power-on/reset: starting WiFi portal");
        scheduler.clear(); // lets start on clean slate (in case of power-on/reset)
        processPortalRequest(config, sleep_manager, scheduler);
    }
    else if (wifi_service_due) 
    {
        TRACE("[main] due WifiService: starting WiFi portal");
        processPortalRequest(config, sleep_manager, scheduler);
    }
    else if (door_action_due) {

        Scheduler::Action action;

        TRACE("[Main] before Scheduler::popDueAction");
        if (!scheduler.popDueAction(now, action)) {
            TRACE("[Main] due door action disappeared before execution");
        }
        else {
#ifdef DEBUG_TRACES
            TRACEF(
                "[Boot] executing action=%s @ %lu",
                actionTypeName(action.type),
                static_cast<unsigned long>(action.timestamp.unixtime())
            );
#endif

            switch (action.type) {
                case Scheduler::ActionType::DoorOpen:
                    TRACE("[Boot] executing DoorOpen");
                    door.open();
                    break;

                case Scheduler::ActionType::DoorClose:
                    TRACE("[Boot] executing DoorClose");
                    door.close();
                    break;

                case Scheduler::ActionType::WifiService:
                    // Not expected here: only due door actions enter this branch.
                    TRACE("[Main] unexpected WifiService in door branch");
                    break;
            }
        }
    }
    else if (wake_cause == SleepManager::WakeCause::OTHER) {

        TRACE("[Boot] unexpected wake cause: starting WiFi portal");
        processPortalRequest(config, sleep_manager, scheduler);
    }
    else if (!rtc_valid) {

        TRACE("[Boot] RTC invalid: starting WiFi portal for recovery");
        processPortalRequest(config, sleep_manager, scheduler);
    }
    else {

        TRACE("[Boot] no due action: no action this wake");
    }

    // Keep at least two future regular door actions scheduled.
    if (sleep_manager.isTimeValid()) {
        const DateTime schedule_now = sleep_manager.now();

        TRACE("[Main] updating schedule after wake");

        if (!scheduler.update_schedule(config, schedule_now)) {
            TRACE("[Main] schedule update after wake failed");
        }
    }

    // Arm the first scheduled action and go back to deep sleep.
    TRACE("[Main] before scheduler.nextAction (final)");
    const Scheduler::Action* next = scheduler.nextAction();
    TRACEF(
        "[Main] nextAction ptr (final)=%p",
        static_cast<void*>(const_cast<Scheduler::Action*>(next))
    );
    ConfigStore::print(config);
    scheduler.print();

    if (next != nullptr) {
        TRACE("[Main] before final sleepUntil");
        sleep_manager.sleepUntil(next->timestamp);
    }

    TRACE("[Main] after sleepUntil : SHOULD NEVER SEE THIS !");
}

void loop()
{
    // Intentionally empty.
}