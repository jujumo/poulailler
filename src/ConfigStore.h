#pragma once

#include <cstdint>

enum class ScheduleMode : uint8_t {
    ABSOLUTE = 0,
    SUN_OFFSET = 1,
};

enum class DoorState : uint8_t {
    UNKNOWN = 0,
    OPEN = 1,
    CLOSED = 2,
};

struct Config {
    float lat = 0.0f;
    float lon = 0.0f;
    int16_t utcOffsetMinutes = 0;

    ScheduleMode openMode = ScheduleMode::ABSOLUTE;
    uint16_t openAbsMinutes = 420;     // 07:00
    int16_t openSunOffsetMinutes = 0;  // relative to sunrise

    ScheduleMode closeMode = ScheduleMode::ABSOLUTE;
    uint16_t closeAbsMinutes = 1140;   // 19:00
    int16_t closeSunOffsetMinutes = 0; // relative to sunset

    DoorState doorState = DoorState::UNKNOWN;
    uint32_t motorRunMs = 15000;

    uint16_t lastOpenDay = 0xFFFF;  // days-since-epoch, 0xFFFF = never
    uint16_t lastCloseDay = 0xFFFF;

    bool configured = false;
};

class ConfigStore {
public:
    void begin();
    Config load();
    void save(const Config& cfg);

private:
    static constexpr const char* NAMESPACE = "doorcfg";
};
