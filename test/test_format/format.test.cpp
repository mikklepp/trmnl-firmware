#include <unity.h>
#include <format.h>
#include <cstring>
#include <cmath>

void setUp(void) {}
void tearDown(void) {}

void test_timer_normal(void) {
    char buf[8];
    formatTimer(buf, sizeof(buf), 120);
    TEST_ASSERT_EQUAL_STRING("2:00", buf);
}

void test_timer_single_digit_seconds(void) {
    char buf[8];
    formatTimer(buf, sizeof(buf), 3);
    TEST_ASSERT_EQUAL_STRING("0:03", buf);
}

void test_timer_zero(void) {
    char buf[8];
    formatTimer(buf, sizeof(buf), 0);
    TEST_ASSERT_EQUAL_STRING("0:00", buf);
}

void test_timer_negative_clamps(void) {
    char buf[8];
    formatTimer(buf, sizeof(buf), -5);
    TEST_ASSERT_EQUAL_STRING("0:00", buf);
}

void test_timer_five_minutes(void) {
    char buf[8];
    formatTimer(buf, sizeof(buf), 300);
    TEST_ASSERT_EQUAL_STRING("5:00", buf);
}

void test_temp_positive(void) {
    char buf[8];
    formatTemp(buf, sizeof(buf), 21.3f);
    TEST_ASSERT_EQUAL_STRING("21.3", buf);
}

void test_temp_negative(void) {
    char buf[8];
    formatTemp(buf, sizeof(buf), -2.1f);
    TEST_ASSERT_EQUAL_STRING("-2.1", buf);
}

void test_temp_zero(void) {
    char buf[8];
    formatTemp(buf, sizeof(buf), 0.0f);
    TEST_ASSERT_EQUAL_STRING("0.0", buf);
}

void test_int_positive(void) {
    char buf[8];
    formatInt(buf, sizeof(buf), 145);
    TEST_ASSERT_EQUAL_STRING("145", buf);
}

void test_int_negative(void) {
    char buf[8];
    formatInt(buf, sizeof(buf), -28);
    TEST_ASSERT_EQUAL_STRING("-28", buf);
}

void test_float1(void) {
    char buf[8];
    formatFloat1(buf, sizeof(buf), 12.8f);
    TEST_ASSERT_EQUAL_STRING("12.8", buf);
}

void test_time_format(void) {
    char buf[8];
    formatTime(buf, sizeof(buf), 12, 34);
    TEST_ASSERT_EQUAL_STRING("12:34", buf);
}

void test_time_leading_zeros(void) {
    char buf[8];
    formatTime(buf, sizeof(buf), 9, 5);
    TEST_ASSERT_EQUAL_STRING("09:05", buf);
}

void test_date_format(void) {
    char buf[8];
    formatDate(buf, sizeof(buf), 4, 2);
    TEST_ASSERT_EQUAL_STRING("04-02", buf);
}

void test_sea_positive(void) {
    // No "+": DSEG7 is a seven-segment face with no plus glyph, so a "+" drew
    // as the .notdef placeholder box on the panel.
    char buf[8];
    formatSea(buf, sizeof(buf), 12);
    TEST_ASSERT_EQUAL_STRING("12", buf);
}

void test_sea_uses_only_glyphs_dseg7_has(void) {
    // The DSEG7 face covers digits, '-', '.', '/', ',' and ':' — nothing else.
    // Anything formatSea emits must come from that set.
    char buf[8];
    const int cases[] = { 0, 7, 12, 99, -1, -5, -42, -99 };
    for (unsigned i = 0; i < sizeof(cases)/sizeof(cases[0]); i++) {
        formatSea(buf, sizeof(buf), cases[i]);
        for (const char* p = buf; *p; p++) {
            bool ok = (*p >= '0' && *p <= '9') || *p == '-';
            TEST_ASSERT_TRUE_MESSAGE(ok, "formatSea emitted a glyph DSEG7 lacks");
        }
    }
}

void test_sea_negative(void) {
    char buf[8];
    formatSea(buf, sizeof(buf), -5);
    TEST_ASSERT_EQUAL_STRING("-5", buf);
}

void test_sea_zero(void) {
    char buf[8];
    formatSea(buf, sizeof(buf), 0);
    TEST_ASSERT_EQUAL_STRING("0", buf);
}

void test_dow_wednesday(void) {
    char buf[4];
    formatDow(buf, sizeof(buf), 3);
    TEST_ASSERT_EQUAL_STRING("WED", buf);
}

void test_dow_sunday(void) {
    char buf[4];
    formatDow(buf, sizeof(buf), 0);
    TEST_ASSERT_EQUAL_STRING("SUN", buf);
}

void test_dow_invalid(void) {
    char buf[4];
    formatDow(buf, sizeof(buf), 9);
    TEST_ASSERT_EQUAL_STRING("---", buf);
}

void test_format_or_dash_nan(void) {
    char buf[8];
    bool was_nan = formatOrDash(buf, sizeof(buf), NAN, formatTemp);
    TEST_ASSERT_TRUE(was_nan);
    TEST_ASSERT_EQUAL_STRING("--", buf);
}

void test_format_or_dash_valid(void) {
    char buf[8];
    bool was_nan = formatOrDash(buf, sizeof(buf), 21.3f, formatTemp);
    TEST_ASSERT_FALSE(was_nan);
    TEST_ASSERT_EQUAL_STRING("21.3", buf);
}


void test_decimal_pads_below_ten(void) {
    // Decimal points must line up down a column, so single-digit values carry a
    // leading figure space. DSEG7's real space advances 8px against a 34px
    // digit, so it cannot align anything — FIGURE_SPACE ('!') maps to an
    // all-segments-off cell with a full digit advance.
    char buf[8];
    formatOrDash1(buf, sizeof(buf), 9.7f);
    TEST_ASSERT_EQUAL_STRING("!9.7", buf);
    formatOrDash1(buf, sizeof(buf), 10.1f);
    TEST_ASSERT_EQUAL_STRING("10.1", buf);
    formatOrDash1(buf, sizeof(buf), 0.4f);
    TEST_ASSERT_EQUAL_STRING("!0.4", buf);
}

void test_decimal_nan_still_dashes(void) {
    char buf[8];
    formatOrDash1(buf, sizeof(buf), NAN);
    TEST_ASSERT_EQUAL_STRING("-", buf);
}

void test_decimal_padded_strings_are_same_length(void) {
    // The whole point: padded and unpadded render to the same width.
    char a[8], b[8];
    formatOrDash1(a, sizeof(a), 5.8f);
    formatOrDash1(b, sizeof(b), 12.8f);
    TEST_ASSERT_EQUAL_INT(strlen(b), strlen(a));
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_timer_normal);
    RUN_TEST(test_timer_single_digit_seconds);
    RUN_TEST(test_timer_zero);
    RUN_TEST(test_timer_negative_clamps);
    RUN_TEST(test_timer_five_minutes);
    RUN_TEST(test_temp_positive);
    RUN_TEST(test_temp_negative);
    RUN_TEST(test_temp_zero);
    RUN_TEST(test_int_positive);
    RUN_TEST(test_int_negative);
    RUN_TEST(test_float1);
    RUN_TEST(test_time_format);
    RUN_TEST(test_time_leading_zeros);
    RUN_TEST(test_date_format);
    RUN_TEST(test_decimal_pads_below_ten);
    RUN_TEST(test_decimal_nan_still_dashes);
    RUN_TEST(test_decimal_padded_strings_are_same_length);
    RUN_TEST(test_sea_positive);
    RUN_TEST(test_sea_uses_only_glyphs_dseg7_has);
    RUN_TEST(test_sea_negative);
    RUN_TEST(test_sea_zero);
    RUN_TEST(test_dow_wednesday);
    RUN_TEST(test_dow_sunday);
    RUN_TEST(test_dow_invalid);
    RUN_TEST(test_format_or_dash_nan);
    RUN_TEST(test_format_or_dash_valid);
    UNITY_END();
    return 0;
}
