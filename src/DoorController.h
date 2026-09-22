#pragma once

#include "Config.h"

// Controls the motor driver.
// No scheduling or position tracking.
class DoorController {
public:
    explicit DoorController(Config& config);

    void begin();
    void jitter();

    void open();
    void close();

private:
    enum class Direction : uint8_t {
        OPEN,
        CLOSE
    };

    void startMoving(Direction direction);
    void stopMoving();
    void operateDoor(uint32_t duration_ms, Direction direction);

    Config& config_;
};