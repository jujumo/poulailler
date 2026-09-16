#pragma once

#include <RTClib.h>

#include "ConfigStore.h"

enum class AlarmOperateDoor : uint8_t {
    no_door_operation,
    door_open,
    door_close,
};

// Thin wrapper around RTClib's RTC_DS3231.
// Alarm1 is always used in "match hours/minutes/seconds, ignore date" mode:
// it fires at the next occurrence of that time-of-day, today or tomorrow,
// so callers never need to reason about which calendar day to arm for.
class RtcManager {
public:
    bool begin();

    // False if the DS3231 lost power (OSF flag) - i.e. never configured or
    // battery/backup was removed. Callers must not schedule from garbage time.
    bool isTimeValid();

    DateTime now();
    void setTime(const DateTime& dt);

    // Arms Alarm1 for the next occurrence of alarmTime's time-of-day and
    // retains its UTC timestamp with the wake reason.
    void setNextAlarm(const DateTime& alarmTime,
                      AlarmOperateDoor operateDoor, bool wifiUp);

    // Returns the operation and WiFi stages recorded when the current
    // deep-sleep alarm was armed. A cold boot has no retained operation.
    AlarmOperateDoor alarmOperateDoor() const;
    bool alarmWifiUp() const;
    uint32_t alarmRequestUnixTime() const;

    // Arms the next RTC alarm and records the staged wake behavior.

    // Clears the Alarm1 fired flag. MUST be called after handling a wake and
    // again immediately before every deep sleep - otherwise the open-drain
    // INT line stays asserted and EXT1 wake fires again instantly.
    void clearAlarm();

private:
    RTC_DS3231 rtc_;
};
