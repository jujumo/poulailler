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

// Called repeatedly from WebPortal::run()'s poll loop, which is now the
// *only* caller (see main.cpp) - every wake opens the portal and lets its
// loop catch the target, rather than main.cpp calling this once directly.
// No-op if the RTC has no valid time yet (first boot, never configured).
// Purely "is now within a few seconds of today's open/close target" - no
// memory of whether it already fired. That's safe because
// armNextAlarmAndSleep() below wakes the device a couple of minutes before
// the target (see kWakeLeadMinutes), so by the time the target actually
// arrives the portal has been polling for a while and the window only
// needs to be a little wider than the poll interval to be caught reliably;
// see the tolerance constants in Scheduler.cpp.
void handleDueActions(Config& cfg, RtcManager& rtc, DoorController& door);

// Computes the soonest of {today's remaining open, today's remaining close,
// tomorrow's open}, arms DS3231 Alarm1 a little before it (kWakeLeadMinutes,
// see Scheduler.cpp) so the device is awake and polling well ahead of the
// actual target, and puts the ESP32 into deep sleep. Also arms a multi-hour
// timer wakeup as a safety net in case the RTC alarm is ever missed. Never
// returns.
[[noreturn]] void armNextAlarmAndSleep(Config& cfg, RtcManager& rtc, ConfigStore& store);

}  // namespace Scheduler
