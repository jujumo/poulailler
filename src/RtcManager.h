#pragma once

#include <RTClib.h>

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

    // Arms Alarm1 for the next occurrence of hour:minute:second and enables
    // the alarm interrupt on the INT/SQW pin.
    void setNextAlarm(uint8_t hour, uint8_t minute, uint8_t second);

    // Clears the Alarm1 fired flag. MUST be called after handling a wake and
    // again immediately before every deep sleep - otherwise the open-drain
    // INT line stays asserted and ext0 wake fires again instantly.
    void clearAlarm();

private:
    RTC_DS3231 rtc_;
};
