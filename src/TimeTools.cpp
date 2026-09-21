#include "TimeTools.h"

#include <Dusk2Dawn.h>
#include <time.h>


namespace TimeTools {

DateTime convert_utc_to_local(const DateTime& timestamp, const float utc_offset)
{
    // Convert the offset from hours to seconds.
    const int32_t offsetSeconds =
        static_cast<int32_t>(utc_offset * 3600.0f);

    return DateTime(timestamp.unixtime() + offsetSeconds);
}


DateTime convert_local_to_utc(const DateTime& timestamp, const float utc_offset)
{
    // Convert the offset from hours to seconds.
    const int32_t offsetSeconds =
        static_cast<int32_t>(utc_offset * 3600.0f);

    return DateTime(timestamp.unixtime() - offsetSeconds);
}


// DateTime <-> timeofday
int convert_time_to_timeofday(const DateTime& timestamp)
{
    return timestamp.hour() * 60 + timestamp.minute();
}

DateTime convert_timeofday_to_time(int timeofday, const DateTime& now)
{
    return DateTime(now.year(), now.month(), now.day(),
        timeofday / 60, timeofday % 60, 0);
}

// Dusk2Dawn returns -1 for polar day/night, and doesn't wrap its result into
// [0, 1440) for extreme timezone/longitude combinations - normalize here so
// callers only ever see well-formed minute-of-day values.
//
// (year, month, day) is a UTC calendar date, and the result is UTC-native:
// passing timezone=0/isDST=false makes Dusk2Dawn return its raw UTC minutes
// (see Dusk2Dawn::sunriseSetUTC()) with no local/DST conversion at all -
// sunrise/sunset is purely a function of lat/lon/date, so no timezone is
// needed here once everything downstream works in UTC too.


// compute sun events in utc
DateTime compute_sunrise_for_today(
    const float latitude, 
    const float longitude,
    const DateTime& now_utc)
{
    Dusk2Dawn location(latitude, longitude, /*timezone=*/0.0f);
    const int year = now_utc.year();
    const int month = now_utc.month();
    const int day = now_utc.day();
    const int minutes_to_evt = location.sunrise(year, month, day, /*isDST=*/false);

    DateTime sunevt_ts(now_utc.year(), now_utc.month(), now_utc.day(),
        minutes_to_evt / 60, minutes_to_evt % 60, 0);
    return sunevt_ts;
}

DateTime compute_sunset_for_today(
    const float latitude, 
    const float longitude,
    const DateTime& now_utc)
{
    Dusk2Dawn location(latitude, longitude, /*timezone=*/0.0f);
    const int year = now_utc.year();
    const int month = now_utc.month();
    const int day = now_utc.day();
    const int minutes_to_evt = location.sunrise(year, month, day, /*isDST=*/false);

    DateTime sunevt_ts(now_utc.year(), now_utc.month(), now_utc.day(),
        minutes_to_evt / 60, minutes_to_evt % 60, 0);
    return sunevt_ts;
}


String convert_timeofday_to_string(int time_of_day) 
{
    char buf[6];
    snprintf(buf, sizeof(buf), "%02d:%02d", time_of_day / 60, time_of_day % 60);
    return String(buf);
}


// Parses "HH:MM" into minutes-since-midnight. Returns -1 on malformed input.
int convert_string_to_timeofday(const String& value) 
{
    int colon = value.indexOf(':');
    if (colon < 1 || colon == value.length() - 1) return -1;
    int hh = value.substring(0, colon).toInt();
    int mm = value.substring(colon + 1).toInt();
    if (hh < 0 || hh > 23 || mm < 0 || mm > 59) return -1;
    return hh * 60 + mm;
}

String convert_time_to_string(const DateTime& now)
{
    char buf[50];
    snprintf(buf, sizeof(buf), "%04d/%02d/%02d-%02d:%02d", 
             now.year(), now.month(), now.day(),
             now.hour(), now.minute()
        );
    return String(buf);
}


}  // namespace TimeTools
