#pragma once

#include <cstddef>
#include <cstdint>

#include <RTClib.h>

#include "Config.h"


class Scheduler {
public:
    enum class ActionType : uint8_t {
        DoorOpen,
        DoorClose,
        WifiService,
        NONE
    };

    struct Action {
        DateTime timestamp;
        ActionType type;
    };

    static constexpr size_t MAX_ACTIONS = 10;

    bool load();
    bool save() const;

    // Rebuild the future door schedule from the current configuration.
    // Existing future actions are preserved; new scheduled door actions are
    // only appended after them.
    bool update_schedule(const Config& config, const DateTime& now);

    bool addAction(const Action& action);
    Action popFirstDueAction(const DateTime& now);

    const Action* nextAction() const;
    const Action* get(size_t index) const;
    bool empty() const;
    size_t count() const;
    void clear();

private:
    static constexpr const char* NAMESPACE = "scheduler";
    static constexpr const char* KEY_ACTIONS = "actions";
    static constexpr const char* KEY_COUNT = "count";

    Action actions_[MAX_ACTIONS];
    size_t count_ = 0;

    void sort();
};

const char* actionTypeName(Scheduler::ActionType type);

String action_to_string(const Scheduler::Action& action);

String scheduler_to_string(const Scheduler& scheduler);
