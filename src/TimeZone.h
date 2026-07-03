#pragma once

#include <RTClib.h>

// Converts between UTC (what RtcManager/the DS3231 stores) and local
// wall-clock time for a named zone from TimeZones.h. DST is resolved by the
// C library's own tz engine (setenv+tzset+localtime/mktime), not hand-rolled
// here - TimeZones.h is the only place per-zone rules live.
namespace TimeZone {

struct LocalTime {
    DateTime dt;
    bool isDst = false;
    int utcOffsetMinutes = 0;  // offset actually in effect at this instant
};

LocalTime toLocal(const DateTime& utc, const char* zoneName);
DateTime toUtc(const DateTime& localWallClock, const char* zoneName);

// The UTC offset in effect at local noon on the given LOCAL calendar date -
// for callers (Scheduler's solar math) that know a date but not yet a
// specific instant. Local noon safely avoids ambiguity right at a DST
// transition, which always happens overnight.
int utcOffsetMinutesForLocalDate(int year, int month, int day, const char* zoneName);

}  // namespace TimeZone
