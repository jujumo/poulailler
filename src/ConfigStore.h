#pragma once

#include <cstdint>

#include "config.h"

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

    // What DoorController last actually did, and when it really happened -
    // self-timestamped by DoorController itself via rtc_.now() at the
    // moment of the move, unconditionally, regardless of who called
    // open()/close() (scheduled or forced). Display-only: shown verbatim in
    // the web UI as the last-event log entry. Nothing gates a decision on
    // these - see lastTriggerUnixTime below for the field that does.
    DoorAction lastOperationAction = DoorAction::NONE;
    uint32_t lastOperationUnixTime = 0;  // UTC unix time; 0 = never

    // Scheduler's debounce key - entirely separate from, and never
    // confused with, lastOperationUnixTime above. Holds the idealized,
    // minute-quantized open/close schedule TARGET (pinned to :00 seconds -
    // never the noisy wall-clock moment the motor actually started) that
    // handleDueActions() last acted on: it compares its freshly-resolved
    // target against this value and only fires if they differ, so two
    // polls landing in the same fire window - or a reboot that lands back
    // in it - don't double-fire. Deliberately narrower than the old
    // day-based "already done today" gates it replaces: any change to the
    // target (a new day, or the same day's schedule shifting by even a
    // minute) is a different value and fires normally. Owned exclusively
    // by Scheduler - DoorController and WebPortal's Force Open/Close never
    // read or write it (a forced move has no schedule target at all). See
    // Scheduler.cpp. Shared between open and close (not one field each)
    // since they're assumed to never resolve to the same target -
    // configuring both to the exact same time-of-day means only whichever
    // Scheduler checks first fires; not a supported configuration.
    uint32_t lastTriggerUnixTime = 0;  // UTC unix time; 0 = never

    uint32_t motorOpenDurationMs = kMotorOpenDurationMs;
    uint32_t motorCloseDurationMs = kMotorCloseDurationMs;
    bool motorInvertDirection = false;  // swap open/close PWM direction

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
