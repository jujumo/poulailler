#pragma once

#include <cstdint>

enum class ScheduleMode : uint8_t {
    ABSOLUTE = 0,
    SUN_OFFSET = 1,
};

// What DoorController last actually did - a historical record for display
// only. Nothing in the firmware gates a decision on it: whether to move is
// entirely up to the caller (Scheduler's lastOpenDay/lastCloseDay for the
// daily schedule, or an explicit force button).
enum class DoorAction : uint8_t {
    NONE = 0,
    OPENED = 1,
    CLOSED = 2,
};

struct Config {
    // Defaults to Grenoble, France - just a reasonable starting point to
    // edit in the config page, not tied to any real deployment.
    float lat = 45.1885f;
    float lon = 5.7245f;
    // Zone name from TimeZones.h. Drives both the local-time display and the
    // sun-time math (via TimeZone.cpp) - see TimeZones.h for why this is a
    // name lookup rather than a raw UTC offset.
    char timezone[24] = "Europe/Paris";

    ScheduleMode openMode = ScheduleMode::ABSOLUTE;
    uint16_t openAbsMinutes = 420;     // 07:00
    int16_t openSunOffsetMinutes = 0;  // relative to sunrise

    ScheduleMode closeMode = ScheduleMode::ABSOLUTE;
    uint16_t closeAbsMinutes = 1140;   // 19:00
    int16_t closeSunOffsetMinutes = 0; // relative to sunset

    // Last completed door move, display-only (see DoorAction above).
    DoorAction lastEventAction = DoorAction::NONE;
    uint32_t lastEventUnixTime = 0;  // UTC unix time; 0 = never
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
