#include "Scheduler.h"

#include <Preferences.h>

namespace {

struct PersistedAction {
    uint32_t timestamp;
    uint8_t type;
};

} // namespace

bool Scheduler::load()
{
    Preferences prefs;

    if (!prefs.begin(NAMESPACE, true)) {
        count_ = 0;
        return false;
    }

    const size_t storedCount = prefs.getUChar(
        KEY_COUNT,
        0
    );

    if (storedCount > MAX_ACTIONS) {
        prefs.end();
        count_ = 0;
        return false;
    }

    PersistedAction storedActions[MAX_ACTIONS];
    const size_t storedSize = storedCount * sizeof(PersistedAction);

    if (storedSize > 0) {
        const size_t actualSize = prefs.getBytes(KEY_ACTIONS, storedActions, storedSize);
        if (actualSize != storedSize) {
            prefs.end();
            count_ = 0;
            return false;
        }
    }

    prefs.end();
    count_ = storedCount;

    for (size_t i = 0; i < count_; ++i) {
        actions_[i].timestamp =  DateTime(storedActions[i].timestamp);
        actions_[i].type = static_cast<ActionType>(storedActions[i].type);
    }

    sort();
    return true;
}

bool Scheduler::save() const
{
    Preferences prefs;
    if (!prefs.begin(NAMESPACE, false)) {
        return false;
    }

    PersistedAction storedActions[MAX_ACTIONS];

    for (size_t i = 0; i < count_; ++i) {
        storedActions[i].timestamp =  actions_[i].timestamp.unixtime();
        storedActions[i].type = static_cast<uint8_t>(actions_[i].type);
    }

    bool success = true;

    if (count_ > 0) {
        const size_t storedSize = count_ * sizeof(PersistedAction);

        if (prefs.putBytes( KEY_ACTIONS, storedActions, storedSize) != storedSize) 
        {
            success = false;
        }
    } else {
        prefs.remove(KEY_ACTIONS);
    }

    if (prefs.putUChar(KEY_COUNT, static_cast<uint8_t>(count_)) != 1)
    {
        success = false;
    }

    prefs.end();
    return success;
}

bool Scheduler::addAction(const Action& action)
{
    if (count_ >= MAX_ACTIONS) {
        return false;
    }

    actions_[count_++] = action;
    sort();
    return save();
}

bool Scheduler::popDueAction(
    const DateTime& now,
    Action& action)
{
    if (count_ == 0) {
        return false;
    }

    if (actions_[0].timestamp.unixtime() >
        now.unixtime()) {
        return false;
    }

    // Return and remove the first action.
    action = actions_[0];

    for (size_t i = 1; i < count_; ++i) {
        actions_[i - 1] = actions_[i];
    }

    --count_;
    // Persist the queue after consuming the action.
    save();
    return true;
}

const Scheduler::Action* Scheduler::nextAction() const
{
    if (count_ == 0) {
        return nullptr;
    }

    return &actions_[0];
}

bool Scheduler::empty() const
{
    return count_ == 0;
}

size_t Scheduler::count() const
{
    return count_;
}

void Scheduler::clear()
{
    count_ = 0;
    save();
}

void Scheduler::sort()
{
    // Insertion sort is sufficient for the small
    // fixed-size action list.
    for (size_t i = 1; i < count_; ++i) {
        Action current = actions_[i];
        size_t j = i;

        while (j > 0 && current.timestamp.unixtime() < actions_[j - 1].timestamp.unixtime())
        {
            actions_[j] = actions_[j - 1];
            --j;
        }

        actions_[j] = current;
    }
}