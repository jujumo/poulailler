#include "RtcManager.h"

#include <Wire.h>

bool RtcManager::begin()
{
    Wire.begin(PIN_RTC_SDA, PIN_RTC_SCL);
    if (!rtc_.begin(&Wire)) {
        return false;
    }
    pinMode(PIN_RTC_SWQ, INPUT_PULLUP);// Alarm output is active-low.
    // We use Alarm1 only.
    rtc_.disableAlarm(2);
    rtc_.clearAlarm(1);
    // Disable the 32 kHz output.
    rtc_.writeSqwPinMode(DS3231_OFF);
    return true;
}

bool RtcManager::isTimeValid()
{
    if (!rtc_.begin(&Wire)) {
        return false;
    }
    const DateTime dt = rtc_.now();
    return dt.year() >= 2020 && dt.year() <= 2099;
}

DateTime RtcManager::now()
{
    return rtc_.now();
}

void RtcManager::setTime(const DateTime& dt)
{
    rtc_.adjust(dt);
}

void RtcManager::setNextAlarm(const DateTime& alarmTime)
{
    // Always clear a previous alarm before arming the next one.
    rtc_.clearAlarm(1);

    // Alarm1 in "Hour" mode ignores the calendar date and
    // triggers at the next occurrence of this time-of-day.
    rtc_.setAlarm1(
        alarmTime,
        DS3231_A1_Hour
    );
}

DateTime RtcManager::getNextalarm()
{
    return rtc_.getAlarm1();
}

void RtcManager::clearAlarm()
{
    rtc_.clearAlarm(1);
}