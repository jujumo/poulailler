#pragma once

#include <cstdint>

// Pure math, no hardware dependencies - safe to unit test on the "native"
// PlatformIO environment.
struct SunTimes {
    int sunriseMinutes = 0;  // minutes since local midnight, [0, 1439]
    int sunsetMinutes = 0;
    bool valid = false;      // false for polar day/night (sun never rises/sets)
};

// NOAA/Meeus low-precision sunrise/sunset algorithm (~1 minute accuracy).
// utcOffsetMinutes is a fixed manual offset - there is no timezone/DST
// database on a device with no internet access.
SunTimes calculateSunTimes(float latitude, float longitude,
                            int year, int month, int day,
                            int16_t utcOffsetMinutes);
