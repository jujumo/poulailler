#include "RtcManager.h"

bool RtcManager::begin() {
    return rtc_.begin();
}

bool RtcManager::isTimeValid() {
    return !rtc_.lostPower();
}

DateTime RtcManager::now() {
    return rtc_.now();
}

void RtcManager::setTime(const DateTime& dt) {
    rtc_.adjust(dt);
}

void RtcManager::setNextAlarm(uint8_t hour, uint8_t minute, uint8_t second) {
    // Date/day fields are ignored in DS3231_A1_Hour mode - only h:m:s matter.
    DateTime alarmTime(2000, 1, 1, hour, minute, second);
    rtc_.clearAlarm(1);
    rtc_.setAlarm1(alarmTime, DS3231_A1_Hour);
}

void RtcManager::clearAlarm() {
    rtc_.clearAlarm(1);
}
