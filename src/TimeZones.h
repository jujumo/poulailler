#pragma once

#include <cstddef>
#include <cstring>

// Curated set of POSIX TZ strings (see `man tzset`, IEEE Std 1003.1) - each
// one encodes a fixed UTC offset *and* that region's DST transition rule, so
// TimeZone.cpp gets correct daylight-saving behavior from the C library's
// own tz engine without a live timezone database, network access, or a
// hand-maintained table of DST switchover dates. This is a practical subset
// of zones, not the full IANA database - to add one, find its exact string
// in glibc's tzdata (e.g. `zdump -v <zone>`, or
// https://github.com/nayarsystems/posix_tz_db) and add a {name, posixTz}
// entry below.
struct TimeZoneEntry {
    const char* name;
    const char* posixTz;
};

constexpr TimeZoneEntry kTimeZones[] = {
    {"UTC", "UTC0"},
    {"Europe/London", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"Europe/Paris", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Helsinki", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Moscow", "MSK-3"},
    {"America/New_York", "EST5EDT,M3.2.0,M11.1.0"},
    {"America/Chicago", "CST6CDT,M3.2.0,M11.1.0"},
    {"America/Denver", "MST7MDT,M3.2.0,M11.1.0"},
    {"America/Los_Angeles", "PST8PDT,M3.2.0,M11.1.0"},
    {"America/Sao_Paulo", "<-03>3"},
    {"Asia/Dubai", "<+04>-4"},
    {"Asia/Kolkata", "IST-5:30"},
    {"Asia/Shanghai", "CST-8"},
    {"Asia/Tokyo", "JST-9"},
    {"Australia/Sydney", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"Pacific/Auckland", "NZST-12NZDT,M9.5.0,M4.1.0/3"},
};
constexpr size_t kTimeZoneCount = sizeof(kTimeZones) / sizeof(kTimeZones[0]);
constexpr const char* kDefaultTimeZoneName = "Europe/Paris";

// Falls back to the default zone if `name` isn't in the table (e.g. stale
// NVS data from a shrunk list), and to plain UTC if even that's missing.
inline const char* posixTzFor(const char* name) {
    for (size_t i = 0; i < kTimeZoneCount; i++) {
        if (strcmp(kTimeZones[i].name, name) == 0) return kTimeZones[i].posixTz;
    }
    for (size_t i = 0; i < kTimeZoneCount; i++) {
        if (strcmp(kTimeZones[i].name, kDefaultTimeZoneName) == 0) return kTimeZones[i].posixTz;
    }
    return "UTC0";
}

inline bool isKnownTimeZoneName(const char* name) {
    for (size_t i = 0; i < kTimeZoneCount; i++) {
        if (strcmp(kTimeZones[i].name, name) == 0) return true;
    }
    return false;
}
