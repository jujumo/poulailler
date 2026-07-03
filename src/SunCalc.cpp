#include "SunCalc.h"

#include <cmath>

namespace {

constexpr double kPi = 3.14159265358979323846;
// "Official" zenith: 90 degrees 50' - accounts for atmospheric refraction
// and the sun's apparent radius.
constexpr double kZenithDeg = 90.8333;

double degToRad(double deg) { return deg * kPi / 180.0; }
double radToDeg(double rad) { return rad * 180.0 / kPi; }

bool isLeapYear(int year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

int dayOfYear(int year, int month, int day) {
    static const int cumulative[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    int n = cumulative[month - 1] + day;
    if (month > 2 && isLeapYear(year)) {
        n += 1;
    }
    return n;
}

// Returns UTC time-of-day in decimal hours [0, 24), or NAN if the event does
// not occur on this day at this latitude (polar day/night).
double computeEventUtcHours(double latRad, double lonDeg, int dayOfYr, bool isSunrise) {
    double lngHour = lonDeg / 15.0;
    double t = isSunrise ? dayOfYr + ((6.0 - lngHour) / 24.0)
                         : dayOfYr + ((18.0 - lngHour) / 24.0);

    double M = 0.9856 * t - 3.289;

    double L = M + 1.916 * sin(degToRad(M)) + 0.020 * sin(degToRad(2 * M)) + 282.634;
    L = fmod(L, 360.0);
    if (L < 0) L += 360.0;

    double RA = radToDeg(atan(0.91764 * tan(degToRad(L))));
    RA = fmod(RA, 360.0);
    if (RA < 0) RA += 360.0;

    // Right ascension must be in the same quadrant as the true longitude.
    double lQuadrant = floor(L / 90.0) * 90.0;
    double raQuadrant = floor(RA / 90.0) * 90.0;
    RA = RA + (lQuadrant - raQuadrant);
    RA = RA / 15.0;  // degrees -> hours

    double sinDec = 0.39782 * sin(degToRad(L));
    double cosDec = cos(asin(sinDec));

    double cosH = (cos(degToRad(kZenithDeg)) - (sinDec * sin(latRad))) / (cosDec * cos(latRad));
    if (cosH > 1.0 || cosH < -1.0) {
        return NAN;  // sun never rises / never sets this day at this latitude
    }

    double H = isSunrise ? 360.0 - radToDeg(acos(cosH)) : radToDeg(acos(cosH));
    H = H / 15.0;

    double T = H + RA - 0.06571 * t - 6.622;

    double UT = fmod(T - lngHour, 24.0);
    if (UT < 0) UT += 24.0;

    return UT;
}

int toLocalMinutes(double utcHours, int16_t utcOffsetMinutes) {
    long minutes = lround(utcHours * 60.0) + utcOffsetMinutes;
    minutes %= 1440;
    if (minutes < 0) minutes += 1440;
    return static_cast<int>(minutes);
}

}  // namespace

SunTimes calculateSunTimes(float latitude, float longitude,
                            int year, int month, int day,
                            int16_t utcOffsetMinutes) {
    SunTimes result;

    // Clamp away from the exact poles to avoid a cos(lat)==0 division.
    double lat = latitude;
    if (lat > 89.9) lat = 89.9;
    if (lat < -89.9) lat = -89.9;
    double latRad = degToRad(lat);

    int doy = dayOfYear(year, month, day);

    double sunriseUtc = computeEventUtcHours(latRad, longitude, doy, true);
    double sunsetUtc = computeEventUtcHours(latRad, longitude, doy, false);

    if (std::isnan(sunriseUtc) || std::isnan(sunsetUtc)) {
        return result;  // valid stays false; caller should keep last known-good value
    }

    result.sunriseMinutes = toLocalMinutes(sunriseUtc, utcOffsetMinutes);
    result.sunsetMinutes = toLocalMinutes(sunsetUtc, utcOffsetMinutes);
    result.valid = true;
    return result;
}
