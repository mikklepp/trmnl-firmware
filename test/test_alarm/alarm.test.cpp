#include <unity.h>
#include <alarm.h>

void setUp(void) {}
void tearDown(void) {}

void test_alarm_sunset_before_curfew(void) {
    // Sunset at 18:30 → alarm at 18:29 (earlier than 20:59)
    AlarmState a = calculateAlarm(18, 30, 12, 0);
    TEST_ASSERT_EQUAL_INT(18, a.hour);
    TEST_ASSERT_EQUAL_INT(29, a.minute);
    TEST_ASSERT_FALSE(a.triggered);
}

void test_alarm_sunset_after_curfew(void) {
    // Sunset at 22:50 → alarm at 20:59 (curfew is earlier)
    AlarmState a = calculateAlarm(22, 50, 12, 0);
    TEST_ASSERT_EQUAL_INT(20, a.hour);
    TEST_ASSERT_EQUAL_INT(59, a.minute);
}

void test_alarm_sunset_at_curfew(void) {
    // Sunset at 21:00 → sunset-1 = 20:59 = curfew (equal, either is fine)
    AlarmState a = calculateAlarm(21, 0, 12, 0);
    TEST_ASSERT_EQUAL_INT(20, a.hour);
    TEST_ASSERT_EQUAL_INT(59, a.minute);
}

void test_alarm_triggered(void) {
    // Sunset at 18:30 → alarm at 18:29, current time IS 18:29
    AlarmState a = calculateAlarm(18, 30, 18, 29);
    TEST_ASSERT_TRUE(a.triggered);
}

void test_alarm_not_triggered(void) {
    AlarmState a = calculateAlarm(18, 30, 18, 28);
    TEST_ASSERT_FALSE(a.triggered);
}

void test_alarm_no_sunset_uses_curfew(void) {
    // Polar night / midnight sun — no valid sunset
    AlarmState a = calculateAlarm(-1, -1, 12, 0);
    TEST_ASSERT_EQUAL_INT(20, a.hour);
    TEST_ASSERT_EQUAL_INT(59, a.minute);
}

void test_alarm_sunset_at_midnight(void) {
    // Sunset at 00:00 → alarm at 23:59 previous day
    AlarmState a = calculateAlarm(0, 0, 12, 0);
    TEST_ASSERT_EQUAL_INT(23, a.hour);
    TEST_ASSERT_EQUAL_INT(59, a.minute);
}

void test_alarm_early_sunset_winter(void) {
    // Sunset at 15:13 → alarm at 15:12
    AlarmState a = calculateAlarm(15, 13, 15, 12);
    TEST_ASSERT_EQUAL_INT(15, a.hour);
    TEST_ASSERT_EQUAL_INT(12, a.minute);
    TEST_ASSERT_TRUE(a.triggered);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_alarm_sunset_before_curfew);
    RUN_TEST(test_alarm_sunset_after_curfew);
    RUN_TEST(test_alarm_sunset_at_curfew);
    RUN_TEST(test_alarm_triggered);
    RUN_TEST(test_alarm_not_triggered);
    RUN_TEST(test_alarm_no_sunset_uses_curfew);
    RUN_TEST(test_alarm_sunset_at_midnight);
    RUN_TEST(test_alarm_early_sunset_winter);
    UNITY_END();
    return 0;
}
