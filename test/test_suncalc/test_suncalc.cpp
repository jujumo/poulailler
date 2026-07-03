#include <unity.h>

#include "SunCalc.h"

void setUp(void) {}
void tearDown(void) {}

// At the equator, day length is ~12h year-round, so sunrise/sunset should
// straddle solar noon (~12:00 local, i.e. ~12:00 UTC at longitude 0) evenly.
void test_equator_equinox_roughly_12_hour_day(void) {
    SunTimes t = calculateSunTimes(0.0f, 0.0f, 2024, 3, 20, 0);
    TEST_ASSERT_TRUE(t.valid);
    TEST_ASSERT_INT_WITHIN(20, 6 * 60, t.sunriseMinutes);
    TEST_ASSERT_INT_WITHIN(20, 18 * 60, t.sunsetMinutes);
}

// Sunrise must precede sunset for an ordinary mid-latitude, non-polar date.
void test_sunrise_before_sunset_midlatitude(void) {
    SunTimes t = calculateSunTimes(48.8566f, 2.3522f, 2024, 6, 21, 0);  // Paris, summer solstice
    TEST_ASSERT_TRUE(t.valid);
    TEST_ASSERT_TRUE(t.sunriseMinutes < t.sunsetMinutes);
}

// A fixed UTC offset must shift both events by exactly that many minutes.
void test_utc_offset_shifts_result(void) {
    SunTimes utc = calculateSunTimes(48.8566f, 2.3522f, 2024, 6, 21, 0);
    SunTimes shifted = calculateSunTimes(48.8566f, 2.3522f, 2024, 6, 21, 60);
    TEST_ASSERT_TRUE(utc.valid);
    TEST_ASSERT_TRUE(shifted.valid);
    TEST_ASSERT_EQUAL_INT((utc.sunriseMinutes + 60) % 1440, shifted.sunriseMinutes);
    TEST_ASSERT_EQUAL_INT((utc.sunsetMinutes + 60) % 1440, shifted.sunsetMinutes);
}

// Above the Arctic Circle at winter solstice, the sun never rises: the
// scheduler must fall back to absolute time rather than use garbage output.
void test_polar_night_is_invalid(void) {
    SunTimes t = calculateSunTimes(70.0f, 0.0f, 2024, 12, 21, 0);
    TEST_ASSERT_FALSE(t.valid);
}

// Latitudes at/near the poles must not crash (cos(lat) division guard).
void test_pole_does_not_crash(void) {
    SunTimes t = calculateSunTimes(90.0f, 0.0f, 2024, 6, 21, 0);
    (void)t;  // no crash is the assertion; validity is latitude/date dependent
    TEST_ASSERT_TRUE(true);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_equator_equinox_roughly_12_hour_day);
    RUN_TEST(test_sunrise_before_sunset_midlatitude);
    RUN_TEST(test_utc_offset_shifts_result);
    RUN_TEST(test_polar_night_is_invalid);
    RUN_TEST(test_pole_does_not_crash);
    return UNITY_END();
}
