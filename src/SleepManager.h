#pragma once

#include <RTClib.h>
#include "Config.h"

class SleepManager {
public:
    // Reason for the current wake-up.
    enum class WakeCause : uint8_t {
        POWER_ON,
        RTC_ALARM,
        OTHER
    };

    // Initialize RTC and wake handling.
    bool begin();

    // Read and set RTC time.
    bool isTimeValid();
    DateTime now();
    void setTime(const DateTime& dt);

    // Return the detected wake cause.
    WakeCause wakeCause() const;

    // Configure and clear the RTC alarm.
    void setNextAlarm(const DateTime& alarmTime);
    void clearAlarm();

    // Arm wake-up and enter deep sleep.
    void sleepUntil(const DateTime& wakeTime);

private:
    RTC_DS3231 rtc_;
    WakeCause wake_cause_ = WakeCause::POWER_ON;
};