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

enum class ScheduleMode : uint8_t {
    TIME_OF_DAY = 0,
    SUN_OFFSET = 1,
};

// What DoorController last did - the "which" half of a historical record;
// see Config::lastOperationAction/lastOperationUnixTime below for the
// other half.
enum class DoorAction : uint8_t {
    NONE = 0,
    OPENED = 1,
    CLOSED = 2,
};

struct Config {
    // Defaults to Grenoble, France - just a reasonable starting point to
    // edit in the config page, not tied to any real deployment.
    float latitude   = DEFAULT_LATITUE;
    float longitude  = DEFAULT_LONGITUDE;
    float utc_offset = DEFAULT_UTC_OFFSET;

    ScheduleMode open_mode = ScheduleMode::TIME_OF_DAY;
    uint16_t open_timeofday = 540;   // UTC time-of-day (540 = 9h = 7h local)
    int16_t open_sun_offset = 0;  // relative to sunrise

    ScheduleMode close_mode = ScheduleMode::SUN_OFFSET;
    uint16_t close_timeofday = 1380;   // UTC time-of-day (1380 = 23h = 21h local)
    int16_t close_sun_offset = 30; // relative to sunset

    uint32_t motor_open_duration_ms = kMotorOpenDurationMs;
    uint32_t motor_close_duration_ms = kMotorCloseDurationMs;

    bool motor_invert_direction = false;  // swap open/close PWM direction

    bool configured = false;
};

namespace ConfigStore {
    void clear();  // Clear all stored config values in case of firmware recompile.
    Config load();   // load from NVS, returning defaults for any missing fields
    bool save(const Config& cfg);
} //namespace ConfigStore
