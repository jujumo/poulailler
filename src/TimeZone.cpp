#include "TimeZone.h"

#include <time.h>

#include "TimeZones.h"

namespace TimeZone {

namespace {

void applyZone(const char* zoneName) {
    setenv("TZ", posixTzFor(zoneName), 1);
    tzset();
}

}  // namespace

LocalTime toLocal(const DateTime& utc, const char* zoneName) {
    applyZone(zoneName);
    time_t epoch = static_cast<time_t>(utc.unixtime());
    struct tm local;
    localtime_r(&epoch, &local);

    LocalTime result;
    result.dt = DateTime(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour,
                          local.tm_min, local.tm_sec);
    result.isDst = local.tm_isdst > 0;
    // This toolchain's newlib doesn't compile in struct tm's tm_gmtoff, so
    // derive the offset instead: reinterpret the local fields as if they
    // were themselves UTC and diff against the real UTC instant.
    long long asIfUtc = static_cast<long long>(result.dt.unixtime());
    result.utcOffsetMinutes = static_cast<int>((asIfUtc - static_cast<long long>(epoch)) / 60);
    return result;
}

DateTime toUtc(const DateTime& localWallClock, const char* zoneName) {
    applyZone(zoneName);
    struct tm local = {};
    local.tm_year = localWallClock.year() - 1900;
    local.tm_mon = localWallClock.month() - 1;
    local.tm_mday = localWallClock.day();
    local.tm_hour = localWallClock.hour();
    local.tm_min = localWallClock.minute();
    local.tm_sec = localWallClock.second();
    local.tm_isdst = -1;  // let mktime() resolve DST for this date/zone
    time_t epoch = mktime(&local);
    return DateTime(static_cast<uint32_t>(epoch));
}

int utcOffsetMinutesForLocalDate(int year, int month, int day, const char* zoneName) {
    applyZone(zoneName);
    struct tm noon = {};
    noon.tm_year = year - 1900;
    noon.tm_mon = month - 1;
    noon.tm_mday = day;
    noon.tm_hour = 12;
    noon.tm_isdst = -1;
    time_t utcEquivalent = mktime(&noon);  // the UTC instant of local noon on this date/zone

    // Same trick as toLocal(): reinterpret the same y/m/d 12:00 fields as if
    // they were UTC, and diff against the real UTC instant just computed.
    DateTime asIfUtc(year, month, day, 12, 0, 0);
    long long offsetSeconds =
        static_cast<long long>(asIfUtc.unixtime()) - static_cast<long long>(utcEquivalent);
    return static_cast<int>(offsetSeconds / 60);
}

}  // namespace TimeZone
