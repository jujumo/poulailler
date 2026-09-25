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

Scheduler::Action scheduled_event_for_day(
    const Config& config,
    const DateTime& day_utc,
    bool opening)
{
    Scheduler::Action door_action;
    const DateTime day_midnight(day_utc.year(), day_utc.month(), day_utc.day(), 0, 0, 0);
    bool closing = !opening;
    door_action.type = opening ? Scheduler::ActionType::DoorOpen 
                               : Scheduler::ActionType::DoorClose;
    // handle time of day envent
    if (opening && config.open_mode == ScheduleMode::TIME_OF_DAY) 
    {
        door_action.timestamp = day_midnight + TimeSpan(config.open_timeofday_utc * 60L);
    }

    if ( closing && config.close_mode == ScheduleMode::TIME_OF_DAY ) 
    {
        door_action.timestamp = day_midnight + TimeSpan(config.close_timeofday_utc * 60L);
    }

    // handle sun event
    const float& lat = config.latitude;
    const float& lon = config.longitude;
    if (opening && config.open_mode == ScheduleMode::SUN_OFFSET) 
    {
        door_action.timestamp = TimeTools::compute_sunrise_for_today(lat, lon, day_midnight) 
                        + TimeSpan(config.open_sun_offset * 60L);
    }
    
    if (closing && config.close_mode == ScheduleMode::SUN_OFFSET) 
    {
        door_action.timestamp = TimeTools::compute_sunset_for_today(lat, lon, day_midnight) 
                        + TimeSpan(config.close_sun_offset * 60L);
    }

    return door_action;
}

bool is_door_action(const Scheduler::Action& action)
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
            timestamp.year(),  timestamp.month(), timestamp.day(),
            timestamp.hour(), timestamp.minute(), timestamp.second(),
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
    size_t nb_door_actions = 0;
    DateTime last_door_action_timestamp = now;
    // count door action in the future
    for (size_t i = 0; i < count_; ++i) {
        const Action& action = actions_[i];
        if (!is_door_action(action)) {
            continue;
        }
        ++nb_door_actions;
        last_door_action_timestamp = action.timestamp;
    }

    TRACEF(
        "[Scheduler] update_schedule: %u scheduled door action(s)",
        static_cast<unsigned>(nb_door_actions)
    );

    // if 2 door action already planned, no need to add more for now
    if (nb_door_actions >= 2) {
        return true;
    }

    // generates candidates open/close actions, then filter out the ones already passed.
    // add the remaining to the schedule (even if theire are more than 2).
    DateTime today(now);
    Action candidate_door_actions[4];
    candidate_door_actions[0] = scheduled_event_for_day(config, today, /*opening=*/true);
    candidate_door_actions[1] = scheduled_event_for_day(config, today, /*opening=*/false);
    DateTime tomorrow = today + TimeSpan(1, 0, 0, 0);
    candidate_door_actions[2] = scheduled_event_for_day(config, tomorrow, /*opening=*/true);
    candidate_door_actions[3] = scheduled_event_for_day(config, tomorrow, /*opening=*/false);
    for (int i=0; i<4; ++i)  
    {
        // only add after last door action timestamp, to avoid adding duplicates
        if (candidate_door_actions[i].timestamp > last_door_action_timestamp)
        { // future
            addAction(candidate_door_actions[i]);
        }
    }
    /////////////////////////////////
    return save();
}

bool Scheduler::addAction(const Action& action)
{
    if (count_ >= MAX_ACTIONS) {
        TRACE("cannot add action : schedule is full.");
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

void Scheduler::print() const
{
    TRACEF("[Scheduler] %u action(s)", static_cast<unsigned>(count_));

    for (size_t i = 0; i < count_; ++i) {
        const Action& action = actions_[i];

        const char* type = "UNKNOWN";

        switch (action.type) {
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

        const DateTime& timestamp = action.timestamp;

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
}
