#include "TimeTools.h"

#include <Dusk2Dawn.h>
#include "Debug.h"
#include <time.h>
#include <cstring>
#include <cstdlib>
#include <cctype>


namespace TimeTools {

namespace {

// Point the libc at the given POSIX TZ rule. Cached: setenv()+tzset() is
// only redone when the string actually changes.
void apply_tz(const char* tz)
{
    static char current[kTimezoneMaxLen] = "";
    if (tz == nullptr || *tz == '\0') {
        tz = "UTC0";
    }
    if (strncmp(current, tz, sizeof(current)) == 0) {
        return;
    }
    strncpy(current, tz, sizeof(current) - 1);
    current[sizeof(current) - 1] = '\0';
    setenv("TZ", current, 1);
    tzset();
}

void to_local_tm(const DateTime& timestamp_utc, const char* tz, struct tm& out)
{
    apply_tz(tz);
    time_t t = static_cast<time_t>(timestamp_utc.unixtime());
    localtime_r(&t, &out);
}

}  // namespace


DateTime convert_utc_to_local(const DateTime& timestamp_utc, const char* tz)
{
    struct tm lt;
    to_local_tm(timestamp_utc, tz, lt);
    return DateTime(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday,
                    lt.tm_hour, lt.tm_min, lt.tm_sec);
}


DateTime convert_local_to_utc(const DateTime& timestamp_local, const char* tz)
{
    apply_tz(tz);
    struct tm lt = {};
    lt.tm_year = timestamp_local.year() - 1900;
    lt.tm_mon  = timestamp_local.month() - 1;
    lt.tm_mday = timestamp_local.day();
    lt.tm_hour = timestamp_local.hour();
    lt.tm_min  = timestamp_local.minute();
    lt.tm_sec  = timestamp_local.second();
    lt.tm_isdst = -1;  // let the libc decide (handles the DST switch days)
    const time_t t = mktime(&lt);
    return DateTime(static_cast<uint32_t>(t));
}


bool is_dst(const DateTime& timestamp_utc, const char* tz)
{
    struct tm lt;
    to_local_tm(timestamp_utc, tz, lt);
    return lt.tm_isdst > 0;
}


String tz_abbreviation(const DateTime& timestamp_utc, const char* tz)
{
    struct tm lt;
    to_local_tm(timestamp_utc, tz, lt);
    char buf[16];
    if (strftime(buf, sizeof(buf), "%Z", &lt) == 0) {
        return String("");
    }
    return String(buf);
}


// newlib's tzset() silently falls back to UTC on a malformed rule, so at
// least reject the obviously wrong inputs here: empty, too long, non
// printable ASCII, or not starting with a std name (>=3 letters or <...>).
bool is_valid_posix_tz(const char* tz)
{
    if (tz == nullptr) return false;
    const size_t len = strnlen(tz, kTimezoneMaxLen);
    if (len == 0 || len >= kTimezoneMaxLen) return false;
    for (size_t i = 0; i < len; ++i) {
        const unsigned char c = static_cast<unsigned char>(tz[i]);
        if (c < 0x21 || c > 0x7E) return false;  // no spaces / control / non-ASCII
        if (c == '\'' || c == '"' || c == '\\') return false;  // never legal in a TZ rule, keeps the HTML simple
    }
    if (tz[0] == '<') {
        return strchr(tz, '>') != nullptr;
    }
    size_t letters = 0;
    while (letters < len && isalpha(static_cast<unsigned char>(tz[letters]))) ++letters;
    return letters >= 3;
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
// [0, 1440) for extreme timezone/longitude combinations - such values are
// reported with a warning and replaced by midnight (UTC) of the given day.
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
    if (minutes_to_evt < 0 || minutes_to_evt >= 24 * 60) {
        TRACEF("[TimeTools] WARNING: invalid sunrise (%d min), using midnight", minutes_to_evt);
        return DateTime(year, month, day, 0, 0, 0);
    }

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
    const int minutes_to_evt = location.sunset(year, month, day, /*isDST=*/false);
    if (minutes_to_evt < 0 || minutes_to_evt >= 24 * 60) {
        TRACEF("[TimeTools] WARNING: invalid sunset (%d min), using midnight", minutes_to_evt);
        return DateTime(year, month, day, 0, 0, 0);
    }

    DateTime sunevt_ts(year, month, day,
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
    snprintf(buf, sizeof(buf), "%04d/%02d/%02d_%02d:%02d:%02d", 
             now.year(), now.month(), now.day(),
             now.hour(), now.minute(), now.second()
        );
    return String(buf);
}


}  // namespace TimeTools
