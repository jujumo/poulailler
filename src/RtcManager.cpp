#include "RtcManager.h"

#include <Wire.h>

#include "Debug.h"
#include "build_config.h"

namespace {
RTC_DATA_ATTR AlarmOperateDoor retainedAlarmOperateDoor = AlarmOperateDoor::no_door_operation;
RTC_DATA_ATTR bool retainedAlarmWifiUp = false;
RTC_DATA_ATTR uint32_t retainedAlarmRequestUnixTime = 0;
}

bool RtcManager::begin() {
    Wire.begin(PIN_RTC_SDA, PIN_RTC_SCL);
    Wire.setTimeOut(100);
    bool connected = rtc_.begin(&Wire);
    TRACEF("[RTC] begin connected=%d", connected);
    return connected;
}

bool RtcManager::isTimeValid() {
    if (rtc_.lostPower()) return false;

    DateTime current = rtc_.now();
    bool valid = current.year() >= 2020 && current.year() <= 2099 && current.month() >= 1 &&
                 current.month() <= 12 && current.day() >= 1 && current.day() <= 31 &&
                 current.hour() <= 23 && current.minute() <= 59 && current.second() <= 59;
    if (!valid) {
        TRACEF("[RTC] invalid read: %04d-%02d-%02d %02d:%02d:%02d", current.year(),
               current.month(), current.day(), current.hour(), current.minute(), current.second());
    }
    return valid;
}

DateTime RtcManager::now() {
    return rtc_.now();
}

void RtcManager::setTime(const DateTime& dt) {
    rtc_.adjust(dt);
}

void RtcManager::setNextAlarm(const DateTime& alarmTime,
                              AlarmOperateDoor operateDoor, bool wifiUp) {
    // Date/day fields are ignored in DS3231_A1_Hour mode; retain the full UTC
    // timestamp so a wake can be checked against the intended occurrence.
    rtc_.clearAlarm(1);
    rtc_.setAlarm1(alarmTime, DS3231_A1_Hour);
    retainedAlarmOperateDoor = operateDoor;
    retainedAlarmWifiUp = wifiUp;
    retainedAlarmRequestUnixTime = alarmTime.unixtime();
}

AlarmOperateDoor RtcManager::alarmOperateDoor() const {
    return retainedAlarmOperateDoor;
}

bool RtcManager::alarmWifiUp() const {
    return retainedAlarmWifiUp;
}

uint32_t RtcManager::alarmRequestUnixTime() const {
    return retainedAlarmRequestUnixTime;
}

void RtcManager::clearAlarm() {
    rtc_.clearAlarm(1);
}
