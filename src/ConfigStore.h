#pragma once

#include <cstdint>

#include "build_config.h"

enum class ScheduleMode : uint8_t {
    ABSOLUTE = 0,
    SUN_OFFSET = 1,
};

// What DoorController last did - the "which" half of a historical record;
// see Config::lastOperationAction/lastOperationUnixTime below for the
// other half.
enum class DoorAction : uint8_t {
    NONE = 0,
    OPENED = 1,
    CLOSED = 2,
};

struct Config {
    // Defaults to Grenoble, France - just a reasonable starting point to
    // edit in the config page, not tied to any real deployment.
    float lat = DEFAULT_LATITUE;
    float lon = DEFAULT_LONGITUDE;
    float utc_offset = DEFAULT_UTC_OFFSET;

    ScheduleMode openMode = ScheduleMode::ABSOLUTE;
    uint16_t openAbsMinutes = 420;     // UTC minute-of-day; local time only in web UI
    int16_t openSunOffsetMinutes = 0;  // relative to sunrise

    ScheduleMode closeMode = ScheduleMode::ABSOLUTE;
    uint16_t closeAbsMinutes = 1140;   // UTC minute-of-day; local time only in web UI
    int16_t closeSunOffsetMinutes = 0; // relative to sunset

    uint32_t motorOpenDurationMs = kMotorOpenDurationMs;
    uint32_t motorCloseDurationMs = kMotorCloseDurationMs;

    bool motorInvertDirection = false;  // swap open/close PWM direction

    bool configured = false;
};

class ConfigStore {
public:
    void begin();   // nothing to do up front 
    void clear();   // Clear all stored config values i ncase of firmware recompile.
    Config load();  // load from NVS, returning defaults for any missing fields
    void save(const Config& cfg);

private:
    static constexpr const char* NAMESPACE = "doorcfg";
};
