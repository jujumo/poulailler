#pragma once

#include "ConfigStore.h"
#include "DoorController.h"
#include "RtcManager.h"

// The scheduling "brain": decides what's due right now, and arms the DS3231
// for the next event before going back to deep sleep.
namespace Scheduler {

// Call after waking from an RTC alarm or the fallback timer. No-op if the
// RTC has no valid time yet (first boot, never configured).
void handleDueActions(Config& cfg, RtcManager& rtc, ConfigStore& store, DoorController& door);

// Computes the soonest of {today's remaining open, today's remaining close,
// tomorrow's open}, arms DS3231 Alarm1 for it, and puts the ESP32 into deep
// sleep. Also arms a multi-hour timer wakeup as a safety net in case the RTC
// alarm is ever missed. Never returns.
[[noreturn]] void armNextAlarmAndSleep(Config& cfg, RtcManager& rtc, ConfigStore& store);

}  // namespace Scheduler
