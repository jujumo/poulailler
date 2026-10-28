#pragma once

#include <cstdint>
#include <cstddef>

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
#define WIFI_AP_PASSWORD ""  // WPA2, >=8 chars

// default config settings
#define DEFAULT_LATITUE 45.1885f
#define DEFAULT_LONGITUDE 5.7245f
// POSIX TZ rule (interpreted by the libc, DST included). Europe/Paris here;
// see the timezone <select> in WebPortalTemplate.h for other common values.
#define DEFAULT_TIMEZONE "CET-1CEST,M3.5.0,M10.5.0/3"
constexpr size_t kTimezoneMaxLen = 48;  // including the trailing NUL

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
    char timezone[kTimezoneMaxLen] = DEFAULT_TIMEZONE;  // POSIX TZ string

    // Fixed times are stored as LOCAL minutes since midnight and converted
    // to UTC day by day by the Scheduler, so they survive DST changes.
    ScheduleMode open_mode = ScheduleMode::TIME_OF_DAY;
    uint16_t open_timeofday_local = 420;   // 07:00 local
    int16_t open_sun_offset = 0;  // relative to sunrise

    ScheduleMode close_mode = ScheduleMode::SUN_OFFSET;
    uint16_t close_timeofday_local = 1260; // 21:00 local
    int16_t close_sun_offset = 30; // relative to sunset

    uint32_t motor_open_duration_ms = kMotorOpenDurationMs;
    uint32_t motor_close_duration_ms = kMotorCloseDurationMs;

    bool motor_invert_direction = false;  // swap open/close PWM direction

    bool configured = false;
};

namespace ConfigStore {
    void clear();  // Clear all stored config values in case of firmware recompile.
    bool resetIfFirmwareChanged();  // clear() on first boot of a newly flashed firmware.
    Config load();   // load from NVS, returning defaults for any missing fields
    bool save(const Config& cfg);
    void print(const Config& cfg);
} //namespace ConfigStore
