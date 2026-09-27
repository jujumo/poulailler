#include "SleepManager.h"
#include "TraceLog.h"
#include "TimeTools.h"
#include "Debug.h"
#include <Wire.h>
#include <esp_sleep.h>

bool SleepManager::begin()
{
    Wire.begin(PIN_RTC_SDA, PIN_RTC_SCL);

    if (!rtc_.begin(&Wire)) {
        return false;
    }

    pinMode(PIN_RTC_SWQ, INPUT_PULLUP);

    // Detect wake cause before clearing the alarm.
    const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

    switch (cause) {
        case ESP_SLEEP_WAKEUP_EXT1:
            wake_cause_ = WakeCause::RTC_ALARM;
            break;

        case ESP_SLEEP_WAKEUP_UNDEFINED:
            wake_cause_ = WakeCause::POWER_ON;
            break;

        default:
            wake_cause_ = WakeCause::OTHER;
            break;
    }

    // Alarm1 is the only RTC alarm used.
    rtc_.disableAlarm(2);
    rtc_.clearAlarm(1);
    rtc_.writeSqwPinMode(DS3231_OFF);

    return true;
}

bool SleepManager::isTimeValid()
{
    const DateTime dt = rtc_.now();
    return dt.year() >= 2020 && dt.year() <= 2099;
}

DateTime SleepManager::now()
{
    return rtc_.now();
}

void SleepManager::setTime(const DateTime& dt)
{
    rtc_.adjust(dt);
}

SleepManager::WakeCause SleepManager::wakeCause() const
{
    return wake_cause_;
}

void SleepManager::setNextAlarm(const DateTime& alarmTime)
{
    // Clear before arming.
    rtc_.clearAlarm(1);

    rtc_.setAlarm1(
        alarmTime,
        DS3231_A1_Date
    );
}

void SleepManager::clearAlarm()
{
    rtc_.clearAlarm(1);
}

void SleepManager::sleepUntil(const DateTime& wake_time)
{
    DateTime alarm_time = wake_time;
    const DateTime near_future_time = rtc_.now() + TimeSpan(0, 0, 0, 3);

    // if alarm tim is already in the past, just wake up in few seconds.
    if (alarm_time <= near_future_time ) {
        alarm_time = near_future_time ;
    }

    setNextAlarm(alarm_time);

    TRACEF("[Sleep] now+esp =%s", TimeTools::convert_time_to_string(near_future_time).c_str());
    TRACEF("[Sleep] alarm   =%s", TimeTools::convert_time_to_string(alarm_time).c_str());
    TRACEF("[Sleep] SWQ before sleep = %d", digitalRead(PIN_RTC_SWQ));

    esp_sleep_enable_ext1_wakeup(
        1ULL << PIN_RTC_SWQ,
        ESP_EXT1_WAKEUP_ANY_LOW
    );
    Serial.flush(); sleep(1);
    esp_deep_sleep_start();
}

const char* wakeCauseName(SleepManager::WakeCause cause)
{
    switch (cause) {
        case SleepManager::WakeCause::POWER_ON:
            return "POWER_ON";
        case SleepManager::WakeCause::RTC_ALARM:
            return "RTC_ALARM";
        default:
            return "OTHER";
    }
}