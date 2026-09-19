#pragma once

#include <cstdint>

// LED indicator (blue LED on most ESP32 boards)
#define PIN_STATUS_LED GPIO_NUM_15

// DS3231 RTC clock
#define PIN_RTC_SDA     GPIO_NUM_19
#define PIN_RTC_SCL     GPIO_NUM_20
#define PIN_RTC_SWQ     GPIO_NUM_1 // on ESP32-C6: 0-7

// DRV883 motor driver
#define PIN_MOTOR_IN1   GPIO_NUM_22
#define PIN_MOTOR_IN2   GPIO_NUM_23
#define PIN_MOTOR_SLEEP GPIO_NUM_21

constexpr uint32_t kMotorCloseDurationMs = 3500;
constexpr uint32_t kMotorOpenDurationMs = 4500;


// WiFi access point served for 5 minutes right after power-on/reset.
#define WIFI_AP_SSID "CoopDoor"
#define WIFI_AP_PASSWORD "123456789"  // WPA2, >=8 chars

// default config settings
#define DEFAULT_LATITUE 45.1885f
#define DEFAULT_LONGITUDE 5.7245f
#define DEFAULT_UTC_OFFSET 2