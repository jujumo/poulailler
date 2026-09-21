#include <Arduino.h>
#include <esp_sleep.h>

#include "Config.h"
#include "Debug.h"
#include "DoorController.h"
#include "RtcManager.h"
#include "Scheduler.h"
#include "TimeTools.h"
#include "WebPortal.h"

namespace {

constexpr unsigned long kConfigPortalDurationMs = 5UL * 60UL * 1000UL;
constexpr uint64_t kRtcWakeMask = (1ULL << PIN_RTC_SWQ);

#ifdef DEBUG_TRACES

const char* wakeCauseName(esp_sleep_wakeup_cause_t cause)
{
    switch (cause) {
        case ESP_SLEEP_WAKEUP_UNDEFINED:
            return "UNDEFINED (power-on/reset/brownout)";
        case ESP_SLEEP_WAKEUP_EXT1:
            return "EXT1 (RTC alarm)";
        case ESP_SLEEP_WAKEUP_TIMER:
            return "TIMER";
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

// Schedule a wake from the DS3231 alarm pin.
[[noreturn]] void sleepUntil(
    RtcManager& rtc,
    const DateTime& alarmTime)
{
    // The DS3231 alarm is programmed before entering deep sleep.
    rtc.setNextAlarm(alarmTime);

    // Make absolutely sure the ESP32 wakes from the DS3231 alarm pin.
    esp_sleep_enable_ext1_wakeup(
        kRtcWakeMask,
        ESP_EXT1_WAKEUP_ANY_LOW
    );

    // Correctness rule: clear the alarm immediately before sleeping.
    rtc.clearAlarm();

    TRACE("[Sleep] entering deep sleep");
    digitalWrite(PIN_STATUS_LED, LOW);
    esp_deep_sleep_start();
    // esp_deep_sleep_start() never returns.
    while (true) {
    }
}

[[noreturn]] void sleepForScheduler(
    Scheduler& scheduler,
    RtcManager& rtc)
{
    const Scheduler::Action* next = scheduler.nextAction();

    if (next == nullptr) {
        TRACE("[Sleep] no scheduled action; sleeping indefinitely");
        rtc.clearAlarm();
        digitalWrite(PIN_STATUS_LED, LOW);
        esp_deep_sleep_start();
        while (true) {
        }
    }

    DateTime now = rtc.now();
    DateTime alarmTime = next->timestamp;

    /*
     * RTC Alarm1 is time-of-day only. If the action is already due,
     * wake again shortly rather than programming an alarm for a
     * potentially incorrect occurrence.
     */
    if (alarmTime.unixtime() <= now.unixtime()) {
        alarmTime = now + TimeSpan(0, 0, 0, 2);
        TRACE("[Sleep] next action already due; retrying in 2 seconds");
    }

#ifdef DEBUG_TRACES
    TRACEF("[Sleep] next=%s @ %lu",
           actionTypeName(next->type),
           static_cast<unsigned long>(
               next->timestamp.unixtime()));

    TRACEF("[Sleep] alarm @ %04d-%02d-%02d %02d:%02d:%02d",
           alarmTime.year(),
           alarmTime.month(),
           alarmTime.day(),
           alarmTime.hour(),
           alarmTime.minute(),
           alarmTime.second());
#endif

    sleepUntil(rtc, alarmTime);
}

} // namespace


void setup()
{
    Serial.begin(115200);
    const unsigned long serialWaitStart = millis();
    while (!Serial && millis() - serialWaitStart < 2000UL) {
        delay(10);
    }

    Serial.println("Bonjour from Poulailler!");
    pinMode(PIN_STATUS_LED, OUTPUT);
    digitalWrite(PIN_STATUS_LED, HIGH);

    //Configuration
    Config config = ConfigStore::load();

    // RTC
    RtcManager rtc;
    if (!rtc.begin()) {
        TRACE("[Boot] RTC initialization failed");
        digitalWrite(PIN_STATUS_LED, LOW);
        while (true) {
            delay(1000);
        }
    }

    // The alarm must be cleared immediately after every wake.
    rtc.clearAlarm();

    // Scheduler
    Scheduler scheduler;
    if (!scheduler.load()) {
        TRACE("[Boot] scheduler load failed");
    }

    // Door controller
    DoorController door(config, rtc);
    door.begin();

    // Wake cause
    const esp_sleep_wakeup_cause_t wakeCause =
        esp_sleep_get_wakeup_cause();

#ifdef DEBUG_TRACES

    TRACEF(
        "[Boot] wake cause=%s",
        wakeCauseName(wakeCause)
    );

    if (rtc.isTimeValid()) {
        const DateTime now = rtc.now();
        TRACEF(
            "[Boot] RTC=%04d-%02d-%02d %02d:%02d:%02d UTC",
            now.year(),
            now.month(),
            now.day(),
            now.hour(),
            now.minute(),
            now.second()
        );

        TRACEF(
            "[Boot] last operation=%s @ %lu",
            doorActionName(cfg.lastOperationAction),
            static_cast<unsigned long>(
                cfg.lastOperationUnixTime)
        );
    } else {
        TRACE("[Boot] RTC time is invalid");
    }

    traceScheduler(scheduler);

#endif

    /*
     * ------------------------------------------------------------------
     * Power-on / reset
     * ------------------------------------------------------------------
     *
     * A real reset starts a bounded WiFi configuration session.
     * A deep-sleep wake must never enter this branch.
     */

    if (wakeCause == ESP_SLEEP_WAKEUP_UNDEFINED) {
        TRACE("[Boot] reset/power-on: starting WiFi portal");
        door.jitter();
        WebPortal portal(config, rtc);
        const WebPortalRequest request =
            portal.run(kConfigPortalDurationMs);

        // The portal may have changed configuration.
        config = ConfigStore::load();

        /*
         * A force operation is deliberately converted into
         * two scheduler actions:
         *
         *   1. immediate door operation
         *   2. WiFi service on the following wake
         *
         * Only one action is consumed per boot.
         */
        if (request == WebPortalRequest::FORCE_OPEN ||
            request == WebPortalRequest::FORCE_CLOSE)
        {
            Scheduler::Action doorAction;
            doorAction.timestamp = DateTime(1);
            doorAction.type =
                request == WebPortalRequest::FORCE_OPEN
                    ? Scheduler::ActionType::DoorOpen
                    : Scheduler::ActionType::DoorClose;

            scheduler.addAction(doorAction);
            Scheduler::Action wifiAction;
            wifiAction.timestamp = DateTime(2);
            wifiAction.type =
                Scheduler::ActionType::WifiService;
            scheduler.addAction(wifiAction);
            TRACE("[Boot] forced door action queued");
        }

        /*
         * NAP is intentionally represented only by a WiFi service
         * request at this level. The portal has already completed its
         * current service session.
         */
        else if (request == WebPortalRequest::NAP) 
        {
            Scheduler::Action wifiAction;
            wifiAction.timestamp = DateTime(2);
            wifiAction.type =
                Scheduler::ActionType::WifiService;

            scheduler.addAction(wifiAction);
            TRACE("[Boot] WiFi service wake queued");
        }

        /*
         * Configuration changes do not require a special action.
         * Continue with the existing scheduler queue.
         */

        sleepForScheduler(scheduler, rtc);
    }

    /*
     * ------------------------------------------------------------------
     * Deep-sleep wake
     * ------------------------------------------------------------------
     *
     * Exactly ONE action is consumed per wake.
     */

    if (!rtc.isTimeValid()) {
        TRACE("[Boot] RTC invalid after deep-sleep wake");

        /*
         * Without a valid RTC we cannot safely execute a scheduled
         * action. Start a service session so the user can correct the
         * clock.
         */

        door.jitter();
        WebPortal portal(config, rtc);
        portal.run(kConfigPortalDurationMs);
        config = ConfigStore::load();
        sleepForScheduler(scheduler, rtc);
    }

    const DateTime now = rtc.now();
    Scheduler::Action action;
    if (!scheduler.popDueAction(now, action))
    {
        /*
         * The RTC woke us but the first scheduler action is not yet
         * due. This can happen because DS3231 Alarm1 is time-of-day
         * based rather than date-based.
         *
         * Never execute an action early.
         */

        TRACE("[Boot] no due scheduler action");
        sleepForScheduler(scheduler, rtc);
    }

#ifdef DEBUG_TRACES

    TRACEF(
        "[Boot] executing action=%s @ %lu",
        actionTypeName(action.type),
        static_cast<unsigned long>(
            action.timestamp.unixtime())
    );

#endif

    /*
     * ------------------------------------------------------------------
     * Execute exactly one action
     * ------------------------------------------------------------------
     */

    switch (action.type) {
        case Scheduler::ActionType::DoorOpen:
            TRACE("[Boot] executing DoorOpen");
            door.open();
            break;

        case Scheduler::ActionType::DoorClose:
            TRACE("[Boot] executing DoorClose");
            door.close();
            break;

        case Scheduler::ActionType::WifiService: {
            TRACE("[Boot] executing WifiService");
            door.jitter();
            WebPortal portal(config, rtc);
            const WebPortalRequest request =
                portal.run(kConfigPortalDurationMs);

            /*
             * Portal may have changed configuration.
             */
            config = ConfigStore::load();

            /*
             * A forced operation requested during this service
             * session is again converted into two deferred actions.
             */
            if (request == WebPortalRequest::FORCE_OPEN ||
                request == WebPortalRequest::FORCE_CLOSE) {
                Scheduler::Action doorAction;
                doorAction.timestamp = DateTime(1);
                doorAction.type =
                    request == WebPortalRequest::FORCE_OPEN
                        ? Scheduler::ActionType::DoorOpen
                        : Scheduler::ActionType::DoorClose;

                scheduler.addAction(doorAction);
                Scheduler::Action wifiAction;
                wifiAction.timestamp = DateTime(2);
                wifiAction.type =
                    Scheduler::ActionType::WifiService;

                scheduler.addAction(wifiAction);
                TRACE("[Boot] forced door action queued");
            }

            else if (request == WebPortalRequest::NAP) {
                Scheduler::Action wifiAction;
                wifiAction.timestamp = DateTime(2);
                wifiAction.type =
                    Scheduler::ActionType::WifiService;

                scheduler.addAction(wifiAction);
                TRACE("[Boot] WiFi service wake queued");
            }

            break;
        }
    }
    // Schedule next wake
    sleepForScheduler(scheduler, rtc);
}


void loop()
{
    // Intentionally empty.
}