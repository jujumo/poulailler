#pragma once

#include <cstdint>
#include <cstddef>

#include <RTClib.h>

#include "Config.h"
#include "DoorController.h"
#include "RtcManager.h"

class Scheduler {
public:
    enum class ActionType : uint8_t {
        DoorOpen,
        DoorClose,
        WifiService
    };

    struct Action {
        DateTime timestamp;
        ActionType type;
    };

    static constexpr size_t MAX_ACTIONS = 10;

    bool load();
    bool save() const;

    bool addAction(const Action& action);
    bool popDueAction(const DateTime& now, Action& action);

    const Action* nextAction() const;

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