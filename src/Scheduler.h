#pragma once

#include "ConfigStore.h"
#include "DoorController.h"
#include "RtcManager.h"

// The Time table keeper : 
namespace Scheduler {

// Returns whether a scheduled wake may actuate. Early wakes are rejected;
// a short post-target grace absorbs RTC/boot delay.
bool scheduledAlarmInWindow(int64_t offsetSeconds);

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
