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

void traceScheduler(const Scheduler& scheduler)
{
    TRACEF("[Scheduler] %u action(s)",
           static_cast<unsigned>(scheduler.count()));

    const Scheduler::Action* action = scheduler.nextAction();
    if (action != nullptr) {
        TRACEF("[Scheduler] next=%s @ %lu",
               actionTypeName(action->type),
               static_cast<unsigned long>(
                   action->timestamp.unixtime()));
    }
}

#endif

void queueRequest(
    Scheduler& scheduler,
    SleepManager& sleep_manager,
    WebPortalRequest request)
{
    if (request == WebPortalRequest::FORCE_OPEN ||
        request == WebPortalRequest::FORCE_CLOSE) {

        const DateTime now = sleep_manager.now();

        Scheduler::Action doorAction;
        doorAction.timestamp = now;
        doorAction.type =
            request == WebPortalRequest::FORCE_OPEN
                ? Scheduler::ActionType::DoorOpen
                : Scheduler::ActionType::DoorClose;

        TRACE("[Main] queueRequest: adding door action");
        scheduler.addAction(doorAction);
        TRACE("[Main] queueRequest: door action added");

        Scheduler::Action wifiAction;
        wifiAction.timestamp = now + TimeSpan(0, 0, 0, 2);
        wifiAction.type = Scheduler::ActionType::WifiService;

        TRACE("[Main] queueRequest: adding WiFi action");
        scheduler.addAction(wifiAction);
        TRACE("[Main] queueRequest: WiFi action added");

        TRACE("[Boot] forced door action queued");
    }
    else if (request == WebPortalRequest::NAP) {
        Scheduler::Action wifiAction;
        wifiAction.timestamp = DateTime(2);
        wifiAction.type = Scheduler::ActionType::WifiService;

        TRACE("[Main] queueRequest: adding WiFi service action");
        scheduler.addAction(wifiAction);
        TRACE("[Main] queueRequest: WiFi service action added");

        TRACE("[Boot] WiFi service wake queued");
    }
}

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
    const WebPortalRequest request =
        runPortal(config, sleep_manager);

    if (request == WebPortalRequest::CONFIG_CHANGED) {
        TRACE("[Main] configuration changed: clearing scheduler");

        scheduler.clear();

        if (sleep_manager.isTimeValid()) {
            const DateTime now = sleep_manager.now();
            TRACE("[Main] rebuilding schedule after configuration change");
            if (!scheduler.update_schedule(config, now)) {
                TRACE("[Main] schedule rebuild failed after configuration change");
            }
        }
        else {
            TRACE(
                "[Main] cannot rebuild schedule after configuration change: "
                "RTC invalid"
            );
        }

        return;
    }

    if (request == WebPortalRequest::RESET_SCHEDULE) {
        TRACE("[Main] resetting scheduler");

        scheduler.clear();

        if (sleep_manager.isTimeValid()) {
            const DateTime now = sleep_manager.now();

            TRACE("[Main] rebuilding schedule after scheduler reset");

            if (!scheduler.update_schedule(config, now)) {
                TRACE("[Main] schedule rebuild failed after scheduler reset");
            }
        }
        else {
            TRACE(
                "[Main] cannot rebuild schedule after scheduler reset: "
                "RTC invalid"
            );
        }

        return;
    }

    TRACE("[Main] before queueRequest");
    queueRequest(scheduler, sleep_manager, request);
    TRACE("[Main] after queueRequest");

    if (sleep_manager.isTimeValid()) {
        const DateTime now = sleep_manager.now();

        TRACE("[Main] updating schedule after portal");

        if (!scheduler.update_schedule(config, now)) {
            TRACE("[Main] schedule update after portal failed");
        }
    }
    else {
        TRACE("[Main] skipping schedule update after portal: RTC invalid");
    }
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
    Serial.println("Bonjour from Poulailler!");
#   endif
    pinMode(PIN_STATUS_LED, OUTPUT);
    digitalWrite(PIN_STATUS_LED, HIGH);

    TRACE("[Boot] before ConfigStore::load");
    Config config = ConfigStore::load();
    TRACE("[Boot] after ConfigStore::load");
#   ifdef DEBUG_TRACES
    Serial.println("[Boot] config loaded!");
#   endif
    TRACE("[Boot] before SleepManager construction");
    SleepManager sleep_manager;
    TRACE("[Boot] after SleepManager construction");
    TRACE("[Boot] before SleepManager::begin");
    if (!sleep_manager.begin()) {
        TRACE("[Boot] SleepManager initialization failed");
        digitalWrite(PIN_STATUS_LED, LOW);
        while (true) {
            delay(1000);
        }
    }
#   ifdef DEBUG_TRACES    
    Serial.println("[Boot] sleep_manager loaded!");
#   endif
    TRACE("[Boot] before Scheduler construction");
    Scheduler scheduler;
    TRACE("[Boot] after Scheduler construction");
    TRACE("[Boot] before Scheduler::load");
    if (!scheduler.load()) {
        TRACE("[Boot] scheduler load failed");
    }
#   ifdef DEBUG_TRACES      
    Serial.println("[Boot] scheduler loaded!");
#   endif
    TRACE("[Boot] before DoorController construction");
    DoorController door(config);
    TRACE("[Boot] after DoorController construction");
    TRACE("[Boot] before DoorController::begin");
    door.begin();
    TRACE("[Boot] after DoorController::begin");
#   ifdef DEBUG_TRACES      
    Serial.println("[Boot] door loaded!");
#   endif
    TRACE("[Boot] before sleep_manager.wakeCause");
    const SleepManager::WakeCause wake_cause = sleep_manager.wakeCause();
    TRACEF("[Boot] wake_cause enum=%d", static_cast<int>(wake_cause));

#   ifdef DEBUG_TRACES

    TRACEF("[Boot] wake cause=%s",
           wakeCauseName(wake_cause));

    if (sleep_manager.isTimeValid()) {
        const DateTime now = sleep_manager.now();
        TRACEF(
            "[Boot] RTC=%04d-%02d-%02d %02d:%02d:%02d UTC",
            now.year(),
            now.month(),
            now.day(),
            now.hour(),
            now.minute(),
            now.second());
    }
    else {
        TRACE("[Boot] RTC time is invalid");
    }

    TRACE("[Boot] before traceScheduler");
    traceScheduler(scheduler);
    TRACE("[Boot] after traceScheduler");

#   endif //DEBUG_TRACES

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

    if (wake_cause == SleepManager::WakeCause::POWER_ON ||
        wifi_service_due) {

        TRACE(
            wake_cause == SleepManager::WakeCause::POWER_ON
                ? "[Boot] power-on/reset: starting WiFi portal"
                : "[Boot] due WifiService: starting WiFi portal"
        );

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

    if (next != nullptr) {
        TRACE("[Main] before final sleepUntil");
        sleep_manager.sleepUntil(next->timestamp);
    }

    TRACE("[Main] scheduler empty after wake");
}

void loop()
{
    // Intentionally empty.
}