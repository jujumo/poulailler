#pragma once

#include "ConfigStore.h"
#include "RtcManager.h"

// Owns the DRV883 pins. Timed movement only - no limit switches, no
// current sensing 

// Always moves the motor when asked - it has no notion of "already there"
// to skip against; whether to call open()/close() at all, and when, is
// entirely the caller's decision (see Scheduler::decideDoorAction() and
// WebPortal's Force Open/Close). After a move, self-timestamps via its own
// RtcManager and records cfg.lastOperationAction (which) and
// cfg.lastOperationUnixTime (when it REALLY happened) unconditionally,
// regardless of caller - display-only, see ConfigStore.h. Never touches
// cfg.lastTriggerUnixTime, which is Scheduler's own debounce bookkeeping,
// not DoorController's concern.
class DoorController {
public:
    DoorController(ConfigStore& store, RtcManager& rtc);

    void begin();
    void signalReady(const Config& cfg);

    void open(Config& cfg);
    void close(Config& cfg);

private:
    void setDirection(DoorAction action, bool invert);
    void run(Config& cfg, DoorAction action);
    void stopMotor();

    ConfigStore& store_;
    RtcManager& rtc_;
};
