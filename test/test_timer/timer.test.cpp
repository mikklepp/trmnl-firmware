#include <unity.h>
#include <timer.h>

void setUp(void) {}
void tearDown(void) {}

void test_timer_initial_state(void) {
    TimerState s = {};
    s.remaining = -1;
    TEST_ASSERT_FALSE(timerActive(s));
}

void test_timer_start_always_preset_zero(void) {
    TimerState s = {};
    s.remaining = -1;
    s.preset_index = 3;  // leftover from previous session
    timerStart(&s);
    TEST_ASSERT_TRUE(timerActive(s));
    TEST_ASSERT_EQUAL_INT(120, s.remaining);  // always 2:00
    TEST_ASSERT_EQUAL_INT(120, s.total);
    TEST_ASSERT_EQUAL_INT(0, s.preset_index);
}

void test_timer_next_advances_preset(void) {
    TimerState s = {};
    s.remaining = -1;
    timerStart(&s);  // preset 0 = 120s

    timerNext(&s);   // preset 1 = 180s
    TEST_ASSERT_EQUAL_INT(180, s.remaining);
    TEST_ASSERT_EQUAL_INT(180, s.total);
    TEST_ASSERT_EQUAL_INT(1, s.preset_index);

    timerNext(&s);   // preset 2 = 240s
    TEST_ASSERT_EQUAL_INT(240, s.remaining);
    TEST_ASSERT_EQUAL_INT(2, s.preset_index);
}

void test_timer_prev_goes_back(void) {
    TimerState s = {};
    s.remaining = -1;
    timerStart(&s);  // preset 0 = 120s

    timerPrev(&s);   // wraps to preset 4 = 60s
    TEST_ASSERT_EQUAL_INT(60, s.remaining);
    TEST_ASSERT_EQUAL_INT(60, s.total);
    TEST_ASSERT_EQUAL_INT(4, s.preset_index);

    timerPrev(&s);   // preset 3 = 300s
    TEST_ASSERT_EQUAL_INT(300, s.remaining);
    TEST_ASSERT_EQUAL_INT(3, s.preset_index);
}

void test_timer_next_wraps(void) {
    TimerState s = {};
    s.remaining = -1;
    timerStart(&s);  // preset 0
    for (int i = 0; i < TIMER_PRESET_COUNT; i++) {
        timerNext(&s);
    }
    // Should wrap back to preset 0
    TEST_ASSERT_EQUAL_INT(120, s.remaining);
    TEST_ASSERT_EQUAL_INT(0, s.preset_index);
}

void test_timer_next_resets_remaining(void) {
    // Right tap while running: remaining resets to full new preset
    TimerState s = {};
    s.remaining = -1;
    timerStart(&s);        // 120s
    timerTick(&s, 60);     // 60s left
    TEST_ASSERT_EQUAL_INT(60, s.remaining);

    timerNext(&s);         // restart at preset 1 = 180s
    TEST_ASSERT_EQUAL_INT(180, s.remaining);
    TEST_ASSERT_EQUAL_INT(180, s.total);
}

void test_timer_tick(void) {
    TimerState s = {};
    s.remaining = -1;
    timerStart(&s);  // 120s

    timerTick(&s, 1);
    TEST_ASSERT_EQUAL_INT(119, s.remaining);
    TEST_ASSERT_FALSE(s.buzzing);
    TEST_ASSERT_TRUE(timerActive(s));
}

void test_timer_done(void) {
    TimerState s = {};
    s.remaining = -1;
    timerStart(&s);  // 120s

    timerTick(&s, 120);
    TEST_ASSERT_TRUE(s.buzzing);
    TEST_ASSERT_FALSE(timerActive(s));
    TEST_ASSERT_EQUAL_INT(-1, s.remaining);
}

void test_timer_overshoot(void) {
    TimerState s = {};
    s.remaining = -1;
    timerStart(&s);  // 120s

    timerTick(&s, 200);  // tick past zero
    TEST_ASSERT_TRUE(s.buzzing);
    TEST_ASSERT_FALSE(timerActive(s));
}

void test_timer_cancel(void) {
    TimerState s = {};
    s.remaining = -1;
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

void test_timer_start_after_buzz_resets_to_preset_zero(void) {
    TimerState s = {};
    s.remaining = -1;
    timerStart(&s);        // 120s, preset 0
    timerNext(&s);         // 180s, preset 1
    timerTick(&s, 180);    // expired
    TEST_ASSERT_TRUE(s.buzzing);

    timerStart(&s);        // new start: always preset 0
    TEST_ASSERT_EQUAL_INT(120, s.remaining);
    TEST_ASSERT_EQUAL_INT(0, s.preset_index);
    TEST_ASSERT_FALSE(s.buzzing);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_timer_initial_state);
    RUN_TEST(test_timer_start_always_preset_zero);
    RUN_TEST(test_timer_next_advances_preset);
    RUN_TEST(test_timer_prev_goes_back);
    RUN_TEST(test_timer_next_wraps);
    RUN_TEST(test_timer_next_resets_remaining);
    RUN_TEST(test_timer_tick);
    RUN_TEST(test_timer_done);
    RUN_TEST(test_timer_overshoot);
    RUN_TEST(test_timer_cancel);
    RUN_TEST(test_timer_tick_when_inactive);
    RUN_TEST(test_timer_start_after_buzz_resets_to_preset_zero);
    UNITY_END();
    return 0;
}
