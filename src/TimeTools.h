#pragma once
#include "Config.h"
#include <RTClib.h> //< for DateTime

// Converts , manipulate times type.
// 2 types of time representation:
//  - DateTime: a full timestamp, in a struct provided by RTC lib
//  - time of day: a number of minutes since 00:00 sored in integer
// Time operations availables:
// - convert UTC <-> Local
// - conpute the time of sunrise/sunset for a given day+position
// - convert DateTime <-> Time of day
// - convert Time (or Time of day) <-> hh:mm string

namespace TimeTools {

// UTC <-> Local
DateTime convert_utc_to_local(const DateTime& timestamp, const float utc_offset);
DateTime convert_local_to_utc(const DateTime& timestamp, const float utc_offset);

// DateTime <-> timeofday
int convert_time_to_timeofday(const DateTime& timestamp);
DateTime convert_timeofday_to_time(int timeofday, const DateTime& now);

// compute sun events in utc
DateTime compute_sunrise_for_today(const float latitude, const float longitude, const DateTime& now_utc);
DateTime compute_sunset_for_today(const float latitude, const float longitude, const DateTime& now_utc);

// string converions
String convert_timeofday_to_string(int time_of_day);
int convert_string_to_timeofday(const String& value);
String convert_time_to_string(const DateTime& now);

}  // TimeTools
