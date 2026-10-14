#include <unity.h>
#include "forecast.h"
#include <cmath>

void setUp(void) {}
void tearDown(void) {}

// ── fillDayForecast ──

static void test_fill_day_basic(void) {
    // 24 hours starting at local 00:00
    float wind[24], gust[24], rain[24];
    int   dir[24];
    for (int i = 0; i < 24; i++) {
        wind[i] = (float)(i % 12) + 1.0f;
        gust[i] = wind[i] * 1.5f;
        rain[i] = 0.0f;
        dir[i]  = 200 + i;
    }
    DayForecast d = {};
    fillDayForecast(&d, wind, gust, rain, dir, 24, 0, 0);

    TEST_ASSERT_EQUAL_INT(24, d.valid_count);
    TEST_ASSERT_TRUE(d.hours[0].valid);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, d.hours[0].wind);
    TEST_ASSERT_EQUAL_INT(0, d.hours[0].hour);
    TEST_ASSERT_EQUAL_INT(23, d.hours[23].hour);
    TEST_ASSERT_EQUAL_INT(223, d.hours[23].dir);
}

static void test_fill_day_offset_start(void) {
    // Series starts at 14:00; hours 0..13 of day 0 have no data.
    float wind[10];
    for (int i = 0; i < 10; i++) wind[i] = 5.0f;
    DayForecast d = {};
    fillDayForecast(&d, wind, NULL, NULL, NULL, 10, 14, 0);

    TEST_ASSERT_FALSE(d.hours[13].valid);
    TEST_ASSERT_TRUE(d.hours[14].valid);
    TEST_ASSERT_TRUE(d.hours[23].valid);
    TEST_ASSERT_EQUAL_INT(10, d.valid_count);
}

static void test_fill_day_second_day(void) {
    // 48 hours from 00:00; day_index 1 is the second 24.
    float wind[48];
    for (int i = 0; i < 48; i++) wind[i] = (float)i;
    DayForecast d = {};
    fillDayForecast(&d, wind, NULL, NULL, NULL, 48, 0, 1);

    TEST_ASSERT_EQUAL_INT(24, d.valid_count);
    TEST_ASSERT_EQUAL_FLOAT(24.0f, d.hours[0].wind);
    TEST_ASSERT_EQUAL_FLOAT(47.0f, d.hours[23].wind);
}

static void test_fill_day_truncated_at_horizon(void) {
    // The design's last strip renders 8 columns, not 12 — a short day must
    // stay short rather than being padded with zeros.
    float wind[32];
    for (int i = 0; i < 32; i++) wind[i] = 6.0f;
    DayForecast d = {};
    fillDayForecast(&d, wind, NULL, NULL, NULL, 32, 0, 1);

    TEST_ASSERT_EQUAL_INT(8, d.valid_count);
    TEST_ASSERT_TRUE(d.hours[7].valid);
    TEST_ASSERT_FALSE(d.hours[8].valid);
}

static void test_fill_day_nan_leaves_slot_invalid(void) {
    float wind[24];
    for (int i = 0; i < 24; i++) wind[i] = 5.0f;
    wind[10] = NAN;
    DayForecast d = {};
    fillDayForecast(&d, wind, NULL, NULL, NULL, 24, 0, 0);

    TEST_ASSERT_FALSE(d.hours[10].valid);
    TEST_ASSERT_EQUAL_INT(23, d.valid_count);
}

static void test_fill_day_null_safe(void) {
    DayForecast d = {};
    fillDayForecast(&d, NULL, NULL, NULL, NULL, 0, 0, 0);
    TEST_ASSERT_EQUAL_INT(0, d.valid_count);
    fillDayForecast(NULL, NULL, NULL, NULL, NULL, 0, 0, 0);  // must not crash
}

// ── computeDaySummary ──

static void test_summary_min_max(void) {
    float wind[24], gust[24], air[24];
    for (int i = 0; i < 24; i++) {
        wind[i] = 4.0f + (float)(i % 8);       // 4..11
        gust[i] = wind[i] + 5.0f;              // max 16
        air[i]  = 9.0f + (float)(i % 7);       // 9..15
    }
    DayForecast d = {};
    fillDayForecast(&d, wind, gust, NULL, NULL, 24, 0, 0);
    computeDaySummary(&d, air, 24, 0, 0);

    TEST_ASSERT_TRUE(d.summary_valid);
    TEST_ASSERT_EQUAL_FLOAT(4.0f,  d.wind_min);
    TEST_ASSERT_EQUAL_FLOAT(11.0f, d.wind_max);
    TEST_ASSERT_EQUAL_FLOAT(16.0f, d.gust_max);
    TEST_ASSERT_EQUAL_FLOAT(9.0f,  d.air_min);
    TEST_ASSERT_EQUAL_FLOAT(15.0f, d.air_max);
}

static void test_summary_sea_range(void) {
    float wind[24];
    int   sea[24];
    for (int i = 0; i < 24; i++) {
        wind[i] = 5.0f;
        sea[i]  = -8 + i;        // -8 .. +15
    }
    DayForecast d = {};
    fillDayForecast(&d, wind, NULL, NULL, NULL, 24, 0, 0);
    fillDaySeaLevel(&d, sea, 24, 0, 0);
    computeDaySummary(&d, NULL, 24, 0, 0);

    TEST_ASSERT_EQUAL_INT(-8, d.sea_min);
    TEST_ASSERT_EQUAL_INT(15, d.sea_max);
}

static void test_summary_invalid_when_no_data(void) {
    DayForecast d = {};
    computeDaySummary(&d, NULL, 0, 0, 0);
    TEST_ASSERT_FALSE(d.summary_valid);
}

// ── fillWindHistory ──

static void test_history_takes_most_recent(void) {
    // 20 hours of observations; the strip shows the last 9, ending at NOW.
    float wind[20], gust[20];
    int   dir[20];
    for (int i = 0; i < 20; i++) {
        wind[i] = (float)i; gust[i] = (float)i + 2.0f; dir[i] = 180;
    }
    WindHistory h = {};
    fillWindHistory(&h, wind, gust, dir, 20, 6);

    TEST_ASSERT_EQUAL_INT(HISTORY_SLOTS, h.count);
    TEST_ASSERT_EQUAL_FLOAT(11.0f, h.slots[0].wind);   // 20-9 = index 11
    TEST_ASSERT_EQUAL_FLOAT(19.0f, h.slots[8].wind);   // last = NOW
}

static void test_history_shorter_than_strip(void) {
    float wind[3] = {4.0f, 5.0f, 6.0f};
    WindHistory h = {};
    fillWindHistory(&h, wind, NULL, NULL, 3, 10);

    TEST_ASSERT_EQUAL_INT(3, h.count);
    TEST_ASSERT_EQUAL_INT(10, h.slots[0].hour);
    TEST_ASSERT_EQUAL_INT(12, h.slots[2].hour);
}

static void test_history_hour_wraps_midnight(void) {
    float wind[4] = {4, 4, 4, 4};
    WindHistory h = {};
    fillWindHistory(&h, wind, NULL, NULL, 4, 22);
    TEST_ASSERT_EQUAL_INT(22, h.slots[0].hour);
    TEST_ASSERT_EQUAL_INT(23, h.slots[1].hour);
    TEST_ASSERT_EQUAL_INT(0,  h.slots[2].hour);
    TEST_ASSERT_EQUAL_INT(1,  h.slots[3].hour);
}

// ── deriveChanges ──

static void test_changes_detects_building(void) {
    float wind[24];
    for (int i = 0; i < 24; i++) wind[i] = 5.0f;
    for (int i = 12; i < 24; i++) wind[i] = 5.0f + (float)(i - 11);  // rises
    DayForecast d = {};
    fillDayForecast(&d, wind, NULL, NULL, NULL, 24, 0, 0);

    ChangesList ch = {};
    int n = deriveChanges(&ch, &d, 0.0f);
    TEST_ASSERT_GREATER_THAN_INT(0, n);
    // Every reported hour rose, so every entry must read BUILDING.
    for (int i = 0; i < n; i++) {
        TEST_ASSERT_EQUAL_INT(CHANGE_BUILDING, ch.entries[i].kind);
    }
}

static void test_changes_detects_easing(void) {
    float wind[24];
    for (int i = 0; i < 24; i++) wind[i] = 15.0f - (float)i * 0.5f;  // falls
    DayForecast d = {};
    fillDayForecast(&d, wind, NULL, NULL, NULL, 24, 0, 0);

    ChangesList ch = {};
    int n = deriveChanges(&ch, &d, 0.0f);
    TEST_ASSERT_GREATER_THAN_INT(0, n);
    for (int i = 0; i < n; i++) {
        TEST_ASSERT_EQUAL_INT(CHANGE_EASING, ch.entries[i].kind);
    }
}

static void test_changes_picks_the_biggest_moves(void) {
    // Mostly flat with three deliberate steps: those are the hours that must
    // surface, regardless of how small the surrounding noise is.
    float wind[24];
    for (int i = 0; i < 24; i++) wind[i] = 8.0f + ((i % 2) ? 0.2f : -0.2f);
    wind[5]  = 14.0f;   // big jump up
    wind[12] = 2.0f;    // big drop
    wind[20] = 12.0f;   // big jump up
    DayForecast d = {};
    fillDayForecast(&d, wind, NULL, NULL, NULL, 24, 0, 0);

    ChangesList ch = {};
    int n = deriveChanges(&ch, &d, 0.0f);
    TEST_ASSERT_EQUAL_INT(CHANGES_MAX, n);

    // Note a spike produces two large deltas — into it and out of it — so the
    // three steps here generate six candidates and only the four biggest
    // survive. Assert on magnitude rather than on specific hours: every entry
    // must be one of the real steps, not a 0.4 m/s wobble.
    for (int i = 0; i < n; i++) {
        int h = ch.entries[i].hour;
        TEST_ASSERT_TRUE(h == 5 || h == 6 || h == 12 || h == 13 ||
                         h == 20 || h == 21);
    }
}

static void test_changes_are_chronological(void) {
    float wind[24];
    for (int i = 0; i < 24; i++) wind[i] = 4.0f + (float)(i % 5);
    DayForecast d = {};
    fillDayForecast(&d, wind, NULL, NULL, NULL, 24, 0, 0);

    ChangesList ch = {};
    int n = deriveChanges(&ch, &d, 0.0f);
    for (int i = 1; i < n; i++) {
        TEST_ASSERT_TRUE(ch.entries[i].hour > ch.entries[i - 1].hour);
    }
}

static void test_changes_caps_at_max(void) {
    // Sawtooth: many reversals, but the list must not overflow.
    float wind[24];
    for (int i = 0; i < 24; i++) wind[i] = (i % 4 < 2) ? 4.0f : 14.0f;
    DayForecast d = {};
    fillDayForecast(&d, wind, NULL, NULL, NULL, 24, 0, 0);

    ChangesList ch = {};
    int n = deriveChanges(&ch, &d, 0.0f);
    TEST_ASSERT_LESS_OR_EQUAL_INT(CHANGES_MAX, n);
}

static void test_changes_null_safe(void) {
    ChangesList ch = {};
    TEST_ASSERT_EQUAL_INT(0, deriveChanges(&ch, NULL, 2.0f));
    TEST_ASSERT_EQUAL_INT(0, deriveChanges(NULL, NULL, 2.0f));
}

// ── compassPoint ──

static void test_compass_cardinals(void) {
    TEST_ASSERT_EQUAL_STRING("N", compassPoint(0));
    TEST_ASSERT_EQUAL_STRING("E", compassPoint(90));
    TEST_ASSERT_EQUAL_STRING("S", compassPoint(180));
    TEST_ASSERT_EQUAL_STRING("W", compassPoint(270));
}

static void test_compass_intercardinals(void) {
    TEST_ASSERT_EQUAL_STRING("SSW", compassPoint(202));
    TEST_ASSERT_EQUAL_STRING("SW",  compassPoint(225));
    TEST_ASSERT_EQUAL_STRING("WSW", compassPoint(247));
}

static void test_compass_wraps(void) {
    // N spans 348.75..11.25, so both ends must land on N.
    TEST_ASSERT_EQUAL_STRING("N", compassPoint(350));
    TEST_ASSERT_EQUAL_STRING("N", compassPoint(360));
    TEST_ASSERT_EQUAL_STRING("N", compassPoint(10));
    // Out-of-range input wraps rather than indexing off the end.
    TEST_ASSERT_EQUAL_STRING("E", compassPoint(450));
    TEST_ASSERT_EQUAL_STRING("W", compassPoint(-90));
}


// ── flag down ──

static void test_flag_down_rolls_window_in_evening(void) {
    // Before the cutoff the pair is today + tomorrow.
    TEST_ASSERT_EQUAL_INT(0, flagDownOffset(9));
    TEST_ASSERT_EQUAL_INT(0, flagDownOffset(17));
    // At and past it, today is nearly spent — show tomorrow + the day after.
    TEST_ASSERT_EQUAL_INT(1, flagDownOffset(FLAG_DOWN_HOUR));
    TEST_ASSERT_EQUAL_INT(1, flagDownOffset(20));
    TEST_ASSERT_EQUAL_INT(1, flagDownOffset(23));
}

static void test_flag_down_avoids_near_empty_strip(void) {
    // At 20:00 a "today" strip has only the 20 and 22 columns left; the rolled
    // window gives a full day instead.
    float wind[50];
    for (int i = 0; i < 50; i++) wind[i] = 6.0f;

    DayForecast today = {};
    fillDayForecast(&today, wind, NULL, NULL, NULL, 50, 20, 0);
    TEST_ASSERT_EQUAL_INT(4, today.valid_count);   // hours 20..23

    DayForecast rolled = {};
    fillDayForecast(&rolled, wind, NULL, NULL, NULL, 50, 20, 1);
    TEST_ASSERT_EQUAL_INT(24, rolled.valid_count);
}


static void test_history_keeps_slot_alignment_over_gaps(void) {
    // A station reporting every 30 minutes leaves every other 15-minute sample
    // NaN. Those slots must stay in place: compacting past them would slide the
    // remaining bars left, so the column labelled -1h would not be -1h.
    float wind[9];
    for (int i = 0; i < 9; i++) wind[i] = (i % 2) ? NAN : 6.0f + (float)i;
    WindHistory h = {};
    fillWindHistory(&h, wind, NULL, NULL, 9, 12);

    TEST_ASSERT_EQUAL_INT(9, h.count);          // all slots present
    TEST_ASSERT_TRUE(h.slots[0].valid);
    TEST_ASSERT_FALSE(h.slots[1].valid);        // gap, not shifted away
    TEST_ASSERT_TRUE(h.slots[2].valid);
    TEST_ASSERT_EQUAL_FLOAT(8.0f, h.slots[2].wind);
    // The NOW column is still the last slot.
    TEST_ASSERT_TRUE(h.slots[8].valid);
    TEST_ASSERT_EQUAL_FLOAT(14.0f, h.slots[8].wind);
}


static void test_changes_hour_matches_its_own_values(void) {
    // Real Itatoukki data: wind rises 0.7 -> 8.3 m/s across the day. Each row
    // must quote the hour whose wind and gust it displays. The first version
    // reported where the run *began* while showing values from where the
    // threshold was crossed, so "06:00 BUILDING 4.2" appeared when 06:00 was
    // actually 0.7 m/s.
    struct { int h; float w, g; } src[] = {
        {6,0.7f,2.5f},{7,2.0f,2.3f},{8,2.2f,2.5f},{9,2.6f,3.4f},
        {10,2.6f,3.1f},{11,3.1f,3.7f},{12,3.3f,4.3f},{13,4.2f,5.1f},
        {14,5.4f,6.5f},{15,6.1f,7.7f},{16,5.9f,7.2f},{17,5.8f,7.0f},
        {18,4.8f,6.8f},{19,4.5f,5.6f},{20,5.0f,6.3f},{21,5.4f,7.3f},
        {22,8.3f,10.1f},
    };
    DayForecast day = {};
    for (unsigned i = 0; i < sizeof(src)/sizeof(src[0]); i++) {
        HourSlot& s = day.hours[src[i].h];
        s.wind = src[i].w; s.gust = src[i].g; s.hour = src[i].h; s.valid = true;
        day.valid_count++;
    }

    ChangesList ch = {};
    int n = deriveChanges(&ch, &day, 0.0f);
    TEST_ASSERT_GREATER_THAN_INT(0, n);
    for (int i = 0; i < n; i++) {
        const ChangeEntry& e = ch.entries[i];
        TEST_ASSERT_TRUE(day.hours[e.hour].valid);
        TEST_ASSERT_EQUAL_FLOAT(day.hours[e.hour].wind, e.wind);
        TEST_ASSERT_EQUAL_FLOAT(day.hours[e.hour].gust, e.gust);
    }
}


static void test_changes_gusts_influence_ranking(void) {
    // Two hours move the base wind identically; only their gusts differ. The
    // gustier one must rank higher, or gusts are not contributing at all.
    DayForecast d = {};
    for (int h = 0; h < 24; h++) {
        d.hours[h].hour = h; d.hours[h].wind = 5.0f; d.hours[h].gust = 6.0f;
        d.hours[h].valid = true; d.valid_count++;
    }
    d.hours[6].wind  = 7.0f; d.hours[6].gust  = 8.0f;   // +2.0 wind, +2.0 gust
    d.hours[7].wind  = 5.0f; d.hours[7].gust  = 6.0f;
    d.hours[15].wind = 7.0f; d.hours[15].gust = 11.0f;  // +2.0 wind, +5.0 gust
    d.hours[16].wind = 5.0f; d.hours[16].gust = 6.0f;

    ChangesList ch = {};
    int n = deriveChanges(&ch, &d, 0.0f);
    TEST_ASSERT_GREATER_THAN_INT(0, n);

    bool saw15 = false;
    for (int i = 0; i < n; i++) if (ch.entries[i].hour == 15) saw15 = true;
    TEST_ASSERT_TRUE_MESSAGE(saw15, "the gustier of two equal wind moves must rank");
}

static void test_changes_survive_missing_gusts(void) {
    // A NaN gust must not zero the score — the hour is still ranked on wind.
    DayForecast d = {};
    for (int h = 0; h < 24; h++) {
        d.hours[h].hour = h; d.hours[h].wind = 4.0f; d.hours[h].gust = NAN;
        d.hours[h].valid = true; d.valid_count++;
    }
    d.hours[12].wind = 12.0f;

    ChangesList ch = {};
    int n = deriveChanges(&ch, &d, 0.0f);
    TEST_ASSERT_GREATER_THAN_INT(0, n);
    bool saw12 = false;
    for (int i = 0; i < n; i++) if (ch.entries[i].hour == 12) saw12 = true;
    TEST_ASSERT_TRUE(saw12);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_fill_day_basic);
    RUN_TEST(test_fill_day_offset_start);
    RUN_TEST(test_fill_day_second_day);
    RUN_TEST(test_fill_day_truncated_at_horizon);
    RUN_TEST(test_fill_day_nan_leaves_slot_invalid);
    RUN_TEST(test_fill_day_null_safe);
    RUN_TEST(test_summary_min_max);
    RUN_TEST(test_summary_sea_range);
    RUN_TEST(test_summary_invalid_when_no_data);
    RUN_TEST(test_history_takes_most_recent);
    RUN_TEST(test_history_shorter_than_strip);
    RUN_TEST(test_history_hour_wraps_midnight);
    RUN_TEST(test_history_keeps_slot_alignment_over_gaps);
    RUN_TEST(test_changes_detects_building);
    RUN_TEST(test_changes_detects_easing);
    RUN_TEST(test_changes_picks_the_biggest_moves);
    RUN_TEST(test_changes_are_chronological);
    RUN_TEST(test_changes_gusts_influence_ranking);
    RUN_TEST(test_changes_survive_missing_gusts);
    RUN_TEST(test_changes_caps_at_max);
    RUN_TEST(test_changes_null_safe);
    RUN_TEST(test_changes_hour_matches_its_own_values);
    RUN_TEST(test_compass_cardinals);
    RUN_TEST(test_compass_intercardinals);
    RUN_TEST(test_compass_wraps);
    RUN_TEST(test_flag_down_rolls_window_in_evening);
    RUN_TEST(test_flag_down_avoids_near_empty_strip);
    return UNITY_END();
}
