#pragma once

#include "ConfigStore.h"

// Owns the BTS7960 pins. Timed movement only - no limit switches, no
// current sensing (R_IS/L_IS left unconnected).
class DoorController {
public:
    DoorController(ConfigStore& store);

    void begin();

    // No-op if cfg.doorState already matches the target, unless force=true
    // (used by the web portal's debug "Force Open/Close" buttons).
    void open(Config& cfg, bool force = false);
    void close(Config& cfg, bool force = false);

private:
    void run(Config& cfg, DoorState target, bool rpwmHigh, bool force);
    void stopMotor();

    ConfigStore& store_;
};
