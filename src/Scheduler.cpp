#include "Scheduler.h"

#include <Preferences.h>

#include "Debug.h"
#include "TimeTools.h"

namespace {

DateTime day_start(const DateTime& timestamp)
{
    return DateTime(
        timestamp.year(),
        timestamp.month(),
        timestamp.day(),
        0,
        0,
        0
    );
}

DateTime scheduled_event_for_day(
    const Config& config,
    const DateTime& day,
    bool opening)
{
    if (
        (opening && config.open_mode == ScheduleMode::TIME_OF_DAY)
        || (!opening && config.close_mode == ScheduleMode::TIME_OF_DAY)
    ) {
        const uint16_t time_of_day =
            opening ? config.open_timeofday : config.close_timeofday;

        return day + TimeSpan(
            0,
            0,
            time_of_day,
            0
        );
    }

    DateTime sun_event;

    if (opening) {
        sun_event = TimeTools::compute_sunrise_for_today(
            config.latitude,
            config.longitude,
            day
        );

        return sun_event + TimeSpan(
            0,
            0,
            config.open_sun_offset,
            0
        );
    }

    sun_event = TimeTools::compute_sunset_for_today(
        config.latitude,
        config.longitude,
        day
    );

    return sun_event + TimeSpan(
        0,
        0,
        config.close_sun_offset,
        0
    );
}

bool is_scheduled_door_action(const Scheduler::Action& action)
{
    if (
        action.type != Scheduler::ActionType::DoorOpen
        && action.type != Scheduler::ActionType::DoorClose
    ) {
        return false;
    }

    // DateTime(1), DateTime(2), ... are reserved for immediate/forced actions.
    return action.timestamp.year() >= 2020;
}

bool same_action(
    const Scheduler::Action& action,
    const DateTime& timestamp,
    Scheduler::ActionType type)
{
    return action.timestamp.unixtime() == timestamp.unixtime()
        && action.type == type;
}

} // namespace

bool Scheduler::load()
{
    Preferences prefs;

    if (!prefs.begin(NAMESPACE, true)) {
        return false;
    }

    const size_t stored_count =
        prefs.getUInt(KEY_COUNT, 0);

    if (stored_count > MAX_ACTIONS) {
        prefs.end();
        count_ = 0;
        return false;
    }

    count_ = stored_count;

    if (count_ > 0) {
        const size_t expected_size = sizeof(Action) * count_;
        const size_t stored_size = prefs.getBytesLength(KEY_ACTIONS);

        if (stored_size != expected_size) {
            prefs.end();
            count_ = 0;
            return false;
        }

        const size_t read_size =
            prefs.getBytes(KEY_ACTIONS, actions_, expected_size);

        if (read_size != expected_size) {
            prefs.end();
            count_ = 0;
            return false;
        }
    }

    prefs.end();
    sort();

    TRACEF("[Scheduler] loaded schedule: %u action(s)",
           static_cast<unsigned>(count_));

    for (size_t i = 0; i < count_; ++i) {
        const char* type = "UNKNOWN";

        switch (actions_[i].type) {
            case ActionType::DoorOpen:
                type = "DoorOpen";
                break;
            case ActionType::DoorClose:
                type = "DoorClose";
                break;
            case ActionType::WifiService:
                type = "WifiService";
                break;
        }

        const DateTime& timestamp = actions_[i].timestamp;

        TRACEF(
            "[Scheduler]   #%u %s @ %04d-%02d-%02d %02d:%02d:%02d (unix=%lu)",
            static_cast<unsigned>(i),
            type,
            timestamp.year(),
            timestamp.month(),
            timestamp.day(),
            timestamp.hour(),
            timestamp.minute(),
            timestamp.second(),
            static_cast<unsigned long>(timestamp.unixtime())
        );
    }

    return true;
}

bool Scheduler::save() const
{
    Preferences prefs;

    if (!prefs.begin(NAMESPACE, false)) {
        return false;
    }

    const size_t written_count =
        prefs.putUInt(KEY_COUNT, static_cast<uint32_t>(count_));

    if (written_count == 0 && count_ != 0) {
        prefs.end();
        return false;
    }

    if (count_ > 0) {
        const size_t expected_size = sizeof(Action) * count_;
        const size_t written_size =
            prefs.putBytes(KEY_ACTIONS, actions_, expected_size);

        if (written_size != expected_size) {
            prefs.end();
            return false;
        }
    }
    else {
        prefs.remove(KEY_ACTIONS);
    }

    prefs.end();
    return true;
}

bool Scheduler::update_schedule(const Config& config, const DateTime& now)
{
    size_t future_scheduled_count = 0;
    DateTime search_after = now;
    bool have_future_scheduled_action = false;

    // Existing scheduled actions are the debounce boundary. New scheduled
    // actions are added only after the last one already in the future.
    for (size_t i = 0; i < count_; ++i) {
        const Action& action = actions_[i];

        if (!is_scheduled_door_action(action)) {
            continue;
        }

        if (action.timestamp.unixtime() <= now.unixtime()) {
            continue;
        }

        ++future_scheduled_count;

        if (
            !have_future_scheduled_action
            || action.timestamp.unixtime() > search_after.unixtime()
        ) {
            search_after = action.timestamp;
            have_future_scheduled_action = true;
        }
    }

    if (future_scheduled_count >= 2) {
        return true;
    }

    TRACEF(
        "[Scheduler] update_schedule: %u future scheduled door action(s)",
        static_cast<unsigned>(future_scheduled_count)
    );

    size_t added_count = 0;
    DateTime day = day_start(search_after);

    if (count_ >= MAX_ACTIONS) {
        TRACE("[Scheduler] update_schedule: action list is full");
        return false;
    }

    // Search forward one day at a time. Each day has at most two scheduled
    // door events: open and close. We always choose the earliest event strictly
    // after the current debounce boundary, then continue from that event.
    while (future_scheduled_count + added_count < 2) {
        DateTime best_timestamp;
        ActionType best_type = ActionType::DoorOpen;
        bool found = false;

        const DateTime open_timestamp =
            scheduled_event_for_day(config, day, true);
        const DateTime close_timestamp =
            scheduled_event_for_day(config, day, false);

        if (open_timestamp.unixtime() > search_after.unixtime()) {
            best_timestamp = open_timestamp;
            best_type = ActionType::DoorOpen;
            found = true;
        }

        if (close_timestamp.unixtime() > search_after.unixtime()) {
            if (
                !found
                || close_timestamp.unixtime() < best_timestamp.unixtime()
            ) {
                best_timestamp = close_timestamp;
                best_type = ActionType::DoorClose;
                found = true;
            }
        }

        if (!found) {
            day = day + TimeSpan(1, 0, 0, 0);
            continue;
        }

        bool duplicate = false;

        for (size_t i = 0; i < count_; ++i) {
            if (
                same_action(
                    actions_[i],
                    best_timestamp,
                    best_type
                )
            ) {
                duplicate = true;
                break;
            }
        }

        if (!duplicate && count_ < MAX_ACTIONS) {
            Action action;
            action.timestamp = best_timestamp;
            action.type = best_type;

            actions_[count_++] = action;
            ++added_count;

#ifdef DEBUG_TRACES
            TRACEF(
                "[Scheduler] added %s @ %lu",
                best_type == ActionType::DoorOpen
                    ? "DoorOpen"
                    : "DoorClose",
                static_cast<unsigned long>(
                    best_timestamp.unixtime()
                )
            );
#endif
        }

        search_after = best_timestamp;
        day = day_start(search_after);
    }

    sort();

    if (added_count == 0) {
        TRACE("[Scheduler] update_schedule: no new action added");
        return future_scheduled_count >= 2 || count_ >= MAX_ACTIONS;
    }

    return save();
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

bool Scheduler::popDueAction(const DateTime& now, Action& action)
{
    if (count_ == 0) {
        return false;
    }

    if (actions_[0].timestamp.unixtime() > now.unixtime()) {
        return false;
    }

    // Return and remove the first action.
    action = actions_[0];

    for (size_t i = 1; i < count_; ++i) {
        actions_[i - 1] = actions_[i];
    }

    --count_;
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
    // Insertion sort is sufficient for the small fixed-size action list.
    for (size_t i = 1; i < count_; ++i) {
        Action current = actions_[i];
        size_t j = i;

        while (
            j > 0
            && current.timestamp.unixtime()
                < actions_[j - 1].timestamp.unixtime()
        ) {
            actions_[j] = actions_[j - 1];
            --j;
        }

        actions_[j] = current;
    }
}
