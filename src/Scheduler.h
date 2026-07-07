#pragma once

#include "ConfigStore.h"
#include "DoorController.h"
#include "RtcManager.h"

// The scheduling "brain": decides what's due right now, and arms the DS3231
// for the next event before going back to deep sleep.
namespace Scheduler {

struct SunTimes {
    int sunriseMinutes = 0;
    int sunsetMinutes = 0;
    bool valid = false;
};

// In-RAM-only record of the last resolved target minute (UTC) that
// handleDueActions() actually fired the motor for, per action. Lets a
// caller that polls handleDueActions() repeatedly within one continuous
// execution (WebPortal::run()) skip re-firing while the resolved target
// hasn't changed. Not a reintroduction of the removed doorState/
// lastOpenDay-lastCloseDay gates: nothing here is persisted to NVS/RTC, so
// it can never survive a sleep cycle, and it keys on the *resolved target
// minute value* rather than a boolean/day marker, so a mid-session
// schedule edit that changes what's due still fires.
struct DueActionTracker {
    int lastOpenFiredMinutes = -1;
    int lastCloseFiredMinutes = -1;
};

// Wraps the Dusk2Dawn library and normalizes its output into a well-formed
// minute-of-day SunTimes; valid=false for polar day/night, where callers
// must fall back to absolute-time config rather than use the output.
SunTimes computeSunTimes(const Config& cfg, int year, int month, int day);

// Resolves a configured open/close schedule (absolute or sun-offset) to a
// UTC minute-of-day for the given UTC calendar day - the same computation
// handleDueActions()/armNextAlarmAndSleep() schedule against, exposed so
// WebPortal can show the user what a schedule actually resolves to.
int resolveUtcMinutes(ScheduleMode mode, uint16_t absMinutes, int16_t sunOffsetMinutes,
                       int sunEventUtcMinutes, bool sunValid, const DateTime& utcDay,
                       const char* zoneName);

// Call after waking from an RTC alarm or the fallback timer (also called
// periodically by WebPortal while the config portal is open). No-op if the
// RTC has no valid time yet (first boot, never configured). Purely "is now
// in today's open/close window". With the default `tracker == nullptr`
// (main.cpp's single per-boot call), there is still no memory of whether
// it already fired, so two separate wakes landing in the same window each
// move the door again - intentional, see DoorController.
//
// `tracker`, when supplied, lets a caller that polls this repeatedly within
// one continuous execution (WebPortal::run()) skip re-firing for a target
// minute it already fired for; see DueActionTracker above for why this
// doesn't reintroduce persisted/day-based idempotency.
void handleDueActions(Config& cfg, RtcManager& rtc, DoorController& door,
                       DueActionTracker* tracker = nullptr);

// Computes the soonest of {today's remaining open, today's remaining close,
// tomorrow's open}, arms DS3231 Alarm1 for it, and puts the ESP32 into deep
// sleep. Also arms a multi-hour timer wakeup as a safety net in case the RTC
// alarm is ever missed. Never returns.
[[noreturn]] void armNextAlarmAndSleep(Config& cfg, RtcManager& rtc, ConfigStore& store);

}  // namespace Scheduler
