#pragma once

#include <RTClib.h>

#include "Config.h"


// Thin wrapper around RTClib's RTC_DS3231.
// Alarm1 is always used in "match hours/minutes/seconds, ignore date" mode:
// it fires at the next occurrence of that time-of-day, today or tomorrow,
// so callers never need to reason about which calendar day to arm for.
class RtcManager {
public:
    bool begin(); // init

    // False if the time read is invalid (e.g. year < 2020 or > 2099).
    bool isTimeValid();

    DateTime now();
    void setTime(const DateTime& dt);

    // Arms Alarm for the next event.
    void setNextAlarm(const DateTime& alarmTime);
    DateTime getNextalarm();

    void clearAlarm();

private:
    RTC_DS3231 rtc_;
};
