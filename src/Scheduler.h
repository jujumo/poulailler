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
// `armNextAlarmAndSleep()` uses to schedule the next wake, exposed so
// WebPortal can show the user what a schedule actually resolves to.
int resolveUtcMinutes(ScheduleMode mode, uint16_t absMinutes, int16_t sunOffsetMinutes,
                       int sunEventUtcMinutes, bool sunValid, const DateTime& utcDay,
                       const char* zoneName);

// Picks cfg's open (isOpen=true) or close (isOpen=false) schedule fields
// and resolves them via resolveUtcMinutes() above - the one place that
// dispatch happens, instead of duplicating "cfg.openX : cfg.closeX" at
// every call site. Used by decideDoorAction()/armNextAlarmAndSleep() below
// and by WebPortal for display.
int resolveScheduleMinutes(const Config& cfg, bool isOpen, const SunTimes& sun,
                            const DateTime& utcDay);

// Computes the soonest of {today's remaining open, today's remaining close,
// tomorrow's open}, arms DS3231 Alarm1 for that target, and puts the ESP32
// into deep sleep. Also arms a multi-hour
// timer wakeup as a safety net in case the RTC alarm is ever missed. Never
// returns.
[[noreturn]] void armNextAlarmAndSleep(Config& cfg, RtcManager& rtc, ConfigStore& store);

// Arms a short WiFi-only wake. It is used as the second stage after a door
// action that requested portal access.
[[noreturn]] void sleepForWifi(RtcManager& rtc, uint32_t seconds = 1);

// Arms a short door-action wake. The next wake performs the operation first;
// if wifiUp is true, it then schedules a separate WiFi-only wake.
[[noreturn]] void sleepForDoorAction(RtcManager& rtc, uint32_t seconds,
                                     AlarmOperateDoor operation, bool wifiUp);

}  // namespace Scheduler
