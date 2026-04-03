#include <unity.h>
#include <timer.h>

void setUp(void) {}
void tearDown(void) {}

void test_timer_initial_state(void) {
    TimerState s = {};
    s.remaining = -1;
    TEST_ASSERT_FALSE(timerActive(s));
}

void test_timer_start_first_preset(void) {
    TimerState s = {};
    s.remaining = -1;
    s.preset_index = 0;
    timerStart(&s);
    TEST_ASSERT_TRUE(timerActive(s));
    TEST_ASSERT_EQUAL_INT(120, s.remaining);
    TEST_ASSERT_EQUAL_INT(120, s.total);
    TEST_ASSERT_EQUAL_INT(1, s.preset_index);  // rotated
}

void test_timer_preset_rotation(void) {
    TimerState s = {};
    s.remaining = -1;
    s.preset_index = 0;

    // Start 5 times — should cycle through all presets
    for (int i = 0; i < TIMER_PRESET_COUNT; i++) {
        s.remaining = -1;
        timerStart(&s);
        TEST_ASSERT_EQUAL_INT(TIMER_PRESETS[i], s.total);
    }
    // 6th start wraps to first preset
    s.remaining = -1;
    timerStart(&s);
    TEST_ASSERT_EQUAL_INT(TIMER_PRESETS[0], s.total);
}

void test_timer_tick(void) {
    TimerState s = {};
    s.remaining = -1;
    s.preset_index = 0;
    timerStart(&s);  // 120s

    timerTick(&s, 1);
    TEST_ASSERT_EQUAL_INT(119, s.remaining);
    TEST_ASSERT_FALSE(s.buzzing);
    TEST_ASSERT_TRUE(timerActive(s));
}

void test_timer_done(void) {
    TimerState s = {};
    s.remaining = -1;
    s.preset_index = 0;
    timerStart(&s);  // 120s

    timerTick(&s, 120);
    TEST_ASSERT_TRUE(s.buzzing);
    TEST_ASSERT_FALSE(timerActive(s));
    TEST_ASSERT_EQUAL_INT(-1, s.remaining);
}

void test_timer_overshoot(void) {
    TimerState s = {};
    s.remaining = -1;
    s.preset_index = 0;
    timerStart(&s);  // 120s

    timerTick(&s, 200);  // tick past zero
    TEST_ASSERT_TRUE(s.buzzing);
    TEST_ASSERT_FALSE(timerActive(s));
}

void test_timer_cancel(void) {
    TimerState s = {};
    s.remaining = -1;
    s.preset_index = 0;
    timerStart(&s);
    timerTick(&s, 30);

    timerCancel(&s);
    TEST_ASSERT_FALSE(timerActive(s));
    TEST_ASSERT_FALSE(s.buzzing);
    TEST_ASSERT_EQUAL_INT(-1, s.remaining);
}

void test_timer_tick_when_inactive(void) {
    TimerState s = {};
    s.remaining = -1;
    timerTick(&s, 1);
    TEST_ASSERT_FALSE(timerActive(s));
    TEST_ASSERT_FALSE(s.buzzing);
}

void test_timer_buzzing_persists_until_read(void) {
    TimerState s = {};
    s.remaining = -1;
    s.preset_index = 0;
    timerStart(&s);
    timerTick(&s, 120);  // done, buzzing=true
    TEST_ASSERT_TRUE(s.buzzing);

    // Buzzing persists — caller is responsible for reading and clearing
    // Starting a new timer clears it
    timerStart(&s);
    TEST_ASSERT_FALSE(s.buzzing);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_timer_initial_state);
    RUN_TEST(test_timer_start_first_preset);
    RUN_TEST(test_timer_preset_rotation);
    RUN_TEST(test_timer_tick);
    RUN_TEST(test_timer_done);
    RUN_TEST(test_timer_overshoot);
    RUN_TEST(test_timer_cancel);
    RUN_TEST(test_timer_tick_when_inactive);
    RUN_TEST(test_timer_buzzing_persists_until_read);
    UNITY_END();
    return 0;
}
