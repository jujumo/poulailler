#pragma once

#include "ConfigStore.h"
#include "RtcManager.h"

// Owns the BTS7960 pins. Timed movement only - no limit switches, no
// current sensing (R_IS/L_IS left unconnected).
//
// Always moves the motor when asked - it has no notion of "already there"
// to skip against, and neither does Scheduler (see handleDueActions()):
// whether to call open()/close() at all is purely "is now in today's
// window", with no memory of whether it already ran. cfg.lastEventAction/
// lastEventUnixTime is updated after a move completes purely as a
// display-only history record, never consulted to decide whether to move.
class DoorController {
public:
    DoorController(ConfigStore& store, RtcManager& rtc);

    void begin();

    void open(Config& cfg);
    void close(Config& cfg);

private:
    void run(Config& cfg, DoorAction action, bool rpwmHigh);
    void stopMotor();

    ConfigStore& store_;
    RtcManager& rtc_;
};
