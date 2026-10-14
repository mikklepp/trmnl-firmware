#include <unity.h>
#include <sunset.h>
#include <cmath>

void setUp(void) {}
void tearDown(void) {}

// Helper: check sunset time is within ±tolerance minutes of expected
static void assertSunsetWithin(int exp_hour, int exp_min, float utc_hours, float utc_offset, int tolerance_min) {
    int hour, minute;
    utcToLocal(utc_hours, utc_offset, &hour, &minute);
    int actual_total = hour * 60 + minute;
    int expected_total = exp_hour * 60 + exp_min;
    int diff = abs(actual_total - expected_total);
    // Handle midnight wrap
    if (diff > 720) diff = 1440 - diff;
    TEST_ASSERT_TRUE_MESSAGE(diff <= tolerance_min,
        "Sunset time outside tolerance");
}

static void assertSunriseWithin(int exp_hour, int exp_min, float utc_hours, float utc_offset, int tolerance_min) {
    int hour, minute;
    utcToLocal(utc_hours, utc_offset, &hour, &minute);
    int actual_total = hour * 60 + minute;
    int expected_total = exp_hour * 60 + exp_min;
    int diff = abs(actual_total - expected_total);
    if (diff > 720) diff = 1440 - diff;
    TEST_ASSERT_TRUE_MESSAGE(diff <= tolerance_min,
        "Sunrise time outside tolerance");
}

// Helsinki (Harmaja): lat 60.105, lon 24.975
// EET = UTC+2, EEST = UTC+3

void test_sunset_helsinki_spring_equinox(void) {
    // 2025-03-20: sunrise ~06:25 EET, sunset ~18:35 EET (UTC+2)
    SunTimes st = calculateSunTimes(2025, 3, 20, 60.105f, 24.975f);
    TEST_ASSERT_TRUE(st.valid);
    assertSunriseWithin(6, 25, st.sunrise_hours, 2.0f, 5);
    assertSunsetWithin(18, 35, st.sunset_hours, 2.0f, 5);
}

void test_sunset_helsinki_midsummer(void) {
    // 2025-06-21: sunrise ~03:54 EEST, sunset ~22:50 EEST (UTC+3)
    SunTimes st = calculateSunTimes(2025, 6, 21, 60.105f, 24.975f);
    TEST_ASSERT_TRUE(st.valid);
    assertSunriseWithin(3, 54, st.sunrise_hours, 3.0f, 5);
    assertSunsetWithin(22, 50, st.sunset_hours, 3.0f, 5);
}

void test_sunset_helsinki_midwinter(void) {
    // 2025-12-21: sunrise ~09:24 EET, sunset ~15:13 EET (UTC+2)
    SunTimes st = calculateSunTimes(2025, 12, 21, 60.105f, 24.975f);
    TEST_ASSERT_TRUE(st.valid);
    assertSunriseWithin(9, 24, st.sunrise_hours, 2.0f, 5);
    assertSunsetWithin(15, 13, st.sunset_hours, 2.0f, 5);
}

void test_sunset_helsinki_april(void) {
    // 2026-04-02: approximate sunset ~20:00 EEST (UTC+3)
    SunTimes st = calculateSunTimes(2026, 4, 2, 60.105f, 24.975f);
    TEST_ASSERT_TRUE(st.valid);
    assertSunsetWithin(20, 0, st.sunset_hours, 3.0f, 10);
}

void test_sunset_utö_summer(void) {
    // Utö (59.779, 21.375) — southwesternmost station
    // 2025-07-01: sunset ~22:55 EEST (UTC+3) — Utö is west, later sunset
    SunTimes st = calculateSunTimes(2025, 7, 1, 59.779f, 21.375f);
    TEST_ASSERT_TRUE(st.valid);
    assertSunsetWithin(22, 55, st.sunset_hours, 3.0f, 10);
}

void test_polar_night_utsjoki(void) {
    // Utsjoki (69.9, 27.0) — northernmost Finland
    // 2025-12-15: polar night, no sunrise/sunset
    SunTimes st = calculateSunTimes(2025, 12, 15, 69.9f, 27.0f);
    TEST_ASSERT_FALSE(st.valid);
}

void test_midnight_sun_utsjoki(void) {
    // Utsjoki (69.9, 27.0)
    // 2025-06-15: midnight sun, no sunset
    SunTimes st = calculateSunTimes(2025, 6, 15, 69.9f, 27.0f);
    TEST_ASSERT_FALSE(st.valid);
}

void test_utc_to_local_basic(void) {
    int h, m;
    utcToLocal(18.75f, 3.0f, &h, &m);
    TEST_ASSERT_EQUAL_INT(21, h);
    TEST_ASSERT_EQUAL_INT(45, m);
}

void test_utc_to_local_wrap(void) {
    int h, m;
    utcToLocal(23.5f, 3.0f, &h, &m);  // 23:30 UTC + 3 = 02:30 next day
    TEST_ASSERT_EQUAL_INT(2, h);
    TEST_ASSERT_EQUAL_INT(30, m);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_sunset_helsinki_spring_equinox);
    RUN_TEST(test_sunset_helsinki_midsummer);
    RUN_TEST(test_sunset_helsinki_midwinter);
    RUN_TEST(test_sunset_helsinki_april);
    RUN_TEST(test_sunset_utö_summer);
    RUN_TEST(test_polar_night_utsjoki);
    RUN_TEST(test_midnight_sun_utsjoki);
    RUN_TEST(test_utc_to_local_basic);
    RUN_TEST(test_utc_to_local_wrap);
    UNITY_END();
    return 0;
}
