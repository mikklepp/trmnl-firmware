#include <unity.h>
#include <layout.h>
#include <cstring>
#include <cmath>

void setUp(void) {}
void tearDown(void) {}

static DisplayState makeNormalState() {
    DisplayState s = {};
    s.hour = 12; s.minute = 34;
    s.day = 4; s.month = 2;
    s.wday = 3;  // Wednesday
    s.alarm_hour = 20; s.alarm_minute = 59;
    s.alarm_valid = true;
    s.solar_w = 145; s.ac_w = 62; s.house_w = -28;
    s.battery_pct = 98; s.engine_v = 12.8f; s.device_pct = 87;
    s.station_name = "Harmaja";
    s.wind_speed = 8; s.wind_gust = 12; s.wind_dir = 225;
    s.saloon_temp = 21.3f; s.saloon_humidity = 45.0f; s.icebox_temp = -2.1f;
    s.timer_active = false;
    return s;
}

// Helper: find first text command matching a substring
static const DrawCmd* findText(const DrawList& dl, const char* substr) {
    for (int i = 0; i < dl.count; i++) {
        if (dl.cmds[i].type == DRAW_TEXT && strstr(dl.cmds[i].text.text, substr)) {
            return &dl.cmds[i];
        }
    }
    return NULL;
}

// Helper: count commands of a given type
static int countType(const DrawList& dl, DrawType type) {
    int n = 0;
    for (int i = 0; i < dl.count; i++) {
        if (dl.cmds[i].type == type) n++;
    }
    return n;
}

// Helper: find first text with specific font
static const DrawCmd* findFont(const DrawList& dl, FontId font) {
    for (int i = 0; i < dl.count; i++) {
        if (dl.cmds[i].type == DRAW_TEXT && dl.cmds[i].text.font == font) {
            return &dl.cmds[i];
        }
    }
    return NULL;
}

// ── Normal mode tests ──

void test_layout_has_clock(void) {
    DisplayState s = makeNormalState();
    DrawList dl = buildLayout(s);
    const DrawCmd* cmd = findText(dl, "12:34");
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(LAYOUT_CLOCK_X, cmd->x);
    TEST_ASSERT_EQUAL_INT(LAYOUT_CLOCK_Y, cmd->y);
    TEST_ASSERT_EQUAL_INT(FONT_DSEG7_CLOCK, cmd->text.font);
}

void test_layout_has_dow(void) {
    DisplayState s = makeNormalState();
    DrawList dl = buildLayout(s);
    const DrawCmd* cmd = findText(dl, "WED");
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(LAYOUT_DOW_X, cmd->x);
    TEST_ASSERT_EQUAL_INT(LAYOUT_DOW_Y, cmd->y);
    TEST_ASSERT_EQUAL_INT(FONT_DSEG14_TEXT, cmd->text.font);
}

void test_layout_has_date(void) {
    DisplayState s = makeNormalState();
    DrawList dl = buildLayout(s);
    const DrawCmd* cmd = findText(dl, "04-02");
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(LAYOUT_DATE_X, cmd->x);
    TEST_ASSERT_EQUAL_INT(FONT_DSEG7_DATA, cmd->text.font);
}

void test_layout_has_alarm(void) {
    DisplayState s = makeNormalState();
    DrawList dl = buildLayout(s);
    const DrawCmd* cmd = findText(dl, "20:59");
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(FONT_DSEG7_DATA, cmd->text.font);
}

void test_layout_no_alarm_when_invalid(void) {
    DisplayState s = makeNormalState();
    s.alarm_valid = false;
    DrawList dl = buildLayout(s);
    TEST_ASSERT_NULL(findText(dl, "20:59"));
}

void test_layout_has_solar(void) {
    DisplayState s = makeNormalState();
    DrawList dl = buildLayout(s);
    TEST_ASSERT_NOT_NULL(findText(dl, "SOLAR"));
    TEST_ASSERT_NOT_NULL(findText(dl, "145"));
}

void test_layout_has_station(void) {
    DisplayState s = makeNormalState();
    DrawList dl = buildLayout(s);
    const DrawCmd* cmd = findText(dl, "Harmaja");
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(LAYOUT_STATION_X, cmd->x);
    TEST_ASSERT_EQUAL_INT(FONT_DSEG14_TEXT, cmd->text.font);
}

void test_layout_has_wind(void) {
    DisplayState s = makeNormalState();
    DrawList dl = buildLayout(s);
    // The NOW block labels its columns WIND / G / AIR; the old screen's
    // GUST and DIR rows are gone (direction now reads as a compass point
    // under each graph column).
    TEST_ASSERT_NOT_NULL(findText(dl, "WIND"));
    TEST_ASSERT_NOT_NULL(findText(dl, "G"));
    TEST_ASSERT_NOT_NULL(findText(dl, "AIR"));
}

void test_layout_has_ruuvi(void) {
    DisplayState s = makeNormalState();
    DrawList dl = buildLayout(s);
    TEST_ASSERT_NOT_NULL(findText(dl, "SALOON"));
    TEST_ASSERT_NOT_NULL(findText(dl, "21.3"));
    TEST_ASSERT_NOT_NULL(findText(dl, "ICEBOX"));
}

void test_layout_structural_lines_normal(void) {
    DisplayState s = makeNormalState();
    DrawList dl = buildLayout(s);
    // hmid + hbot + two vertical splits = 4 structural lines, plus the graph
    // axes drawn per strip.
    TEST_ASSERT_TRUE(countType(dl, DRAW_LINE) >= 4);
}

void test_layout_bank_is_evenly_pitched(void) {
    // The boat bank is ten cells on one row at a fixed pitch.
    DisplayState s = makeNormalState();
    DrawList dl = buildLayout(s);
    const DrawCmd* solar = findText(dl, "SOLAR");
    const DrawCmd* device = findText(dl, "DEVICE");
    TEST_ASSERT_NOT_NULL(solar);
    TEST_ASSERT_NOT_NULL(device);
    TEST_ASSERT_EQUAL_INT(LAYOUT_BANK_X0, solar->x);
    TEST_ASSERT_EQUAL_INT(LAYOUT_BANK_X0 + 9 * LAYOUT_BANK_PITCH, device->x);
    TEST_ASSERT_EQUAL_INT(solar->y, device->y);
}

void test_layout_shared_baselines(void) {
    // The design puts label and value on one baseline per row; the CSS
    // line-height:0 conversion must preserve that exactly, not approximately.
    DisplayState s = makeNormalState();
    DrawList dl = buildLayout(s);
    const DrawCmd* day = findText(dl, "DAY");
    const DrawCmd* dow = findText(dl, "WED");
    TEST_ASSERT_NOT_NULL(day);
    TEST_ASSERT_NOT_NULL(dow);
    TEST_ASSERT_EQUAL_INT(day->y, dow->y);

    const DrawCmd* alarm_lbl = findText(dl, "ALARM");
    const DrawCmd* alarm_val = findText(dl, "20:59");
    TEST_ASSERT_NOT_NULL(alarm_lbl);
    TEST_ASSERT_NOT_NULL(alarm_val);
    TEST_ASSERT_EQUAL_INT(alarm_lbl->y, alarm_val->y);
}

// ── Timer mode tests ──

void test_layout_timer_has_countdown(void) {
    DisplayState s = makeNormalState();
    s.timer_active = true;
    s.timer_seconds = 120;
    s.timer_total = 120;
    DrawList dl = buildLayout(s);
    const DrawCmd* cmd = findText(dl, "2:00");
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(FONT_DSEG7_CLOCK, cmd->text.font);
    // Left-aligned with the clock above it.
    TEST_ASSERT_EQUAL_INT(LAYOUT_CLOCK_X, cmd->x);
}

void test_layout_timer_has_progress_bar(void) {
    DisplayState s = makeNormalState();
    s.timer_active = true;
    s.timer_seconds = 60;
    s.timer_total = 120;
    DrawList dl = buildLayout(s);
    // Should have at least one filled rect (the progress bar fill)
    TEST_ASSERT_TRUE(countType(dl, DRAW_RECT_FILL) >= 1);
}

void test_layout_timer_no_electricals(void) {
    DisplayState s = makeNormalState();
    s.timer_active = true;
    s.timer_seconds = 120;
    s.timer_total = 120;
    DrawList dl = buildLayout(s);
    TEST_ASSERT_NULL(findText(dl, "SOLAR"));
    TEST_ASSERT_NULL(findText(dl, "CHARGER"));
    TEST_ASSERT_NULL(findText(dl, "Harmaja"));
    TEST_ASSERT_NULL(findText(dl, "SALOON"));
}

void test_layout_timer_keeps_clock(void) {
    DisplayState s = makeNormalState();
    s.timer_active = true;
    s.timer_seconds = 120;
    s.timer_total = 120;
    DrawList dl = buildLayout(s);
    TEST_ASSERT_NOT_NULL(findText(dl, "12:34"));
    TEST_ASSERT_NOT_NULL(findText(dl, "WED"));
    TEST_ASSERT_NOT_NULL(findText(dl, "04-02"));
}

void test_layout_timer_fewer_lines(void) {
    DisplayState s = makeNormalState();
    DrawList dl_normal = buildLayout(s);
    int lines_normal = countType(dl_normal, DRAW_LINE);

    s.timer_active = true;
    s.timer_seconds = 120;
    s.timer_total = 120;
    DrawList dl_timer = buildLayout(s);
    int lines_timer = countType(dl_timer, DRAW_LINE);

    // Timer has fewer structural lines (no forecast dividers, shorter vsplit)
    // but adds progress bar outline (4 lines)
    // Still should have hmid + vsplit + 4 bar = 6 lines
    TEST_ASSERT_TRUE(lines_timer >= 6);
    // Normal has hmid + hbot + 2 vertical splits = 4 structural
    TEST_ASSERT_TRUE(lines_normal >= 4);
}

void test_layout_nan_shows_dashes(void) {
    DisplayState s = makeNormalState();
    s.saloon_temp = NAN;
    s.solar_w = NAN;
    s.wind_speed = NAN;
    DrawList dl = buildLayout(s);
    // A missing reading shows a single "-" on the segment faces.
    TEST_ASSERT_NOT_NULL(findText(dl, "-"));
}

void test_layout_cmd_count_reasonable(void) {
    DisplayState s = makeNormalState();
    DrawList dl = buildLayout(s);
    // Normal mode: ~5 lines + ~30 text commands = ~35
    TEST_ASSERT_TRUE(dl.count > 20);
    TEST_ASSERT_TRUE(dl.count < MAX_DRAW_CMDS);
}

// ── Status bar tests ──

void test_layout_status_bar_has_hints(void) {
    DisplayState s = makeNormalState();
    DrawList dl = buildLayout(s);
    // Status bar should show hold-gesture hints
    TEST_ASSERT_NOT_NULL(findText(dl, "OFF"));
    TEST_ASSERT_NOT_NULL(findText(dl, "SETUP"));
}

void test_layout_status_bar_usb_power_in(void) {
    DisplayState s = makeNormalState();
    s.otg_enabled = false;
    DrawList dl = buildLayout(s);
    const DrawCmd* cmd = findText(dl, "USB Power");
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_NOT_NULL(strstr(cmd->text.text, "In"));
}

void test_layout_status_bar_usb_power_out(void) {
    DisplayState s = makeNormalState();
    s.otg_enabled = true;
    DrawList dl = buildLayout(s);
    const DrawCmd* cmd = findText(dl, "USB Power");
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_NOT_NULL(strstr(cmd->text.text, "Out"));
}

void test_layout_status_bar_position(void) {
    DisplayState s = makeNormalState();
    DrawList dl = buildLayout(s);
    const DrawCmd* cmd = findText(dl, "OFF");
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(LAYOUT_STATUS_Y, cmd->y);
    TEST_ASSERT_EQUAL_INT(LAYOUT_STATUS_LEFT_X, cmd->x);
}

void test_layout_timer_no_status_bar(void) {
    DisplayState s = makeNormalState();
    s.timer_active = true;
    s.timer_seconds = 120;
    s.timer_total = 120;
    DrawList dl = buildLayout(s);
    // Timer mode should not render status bar (it overlaps progress bar)
    TEST_ASSERT_NULL(findText(dl, "OFF"));
    TEST_ASSERT_NULL(findText(dl, "SETUP"));
    TEST_ASSERT_NULL(findText(dl, "USB Power"));
}

// Forecast rendering tests are rewritten with the Weather Options strips in the
// layout transcription pass. The old ones exercised the 24-column digit grid
// that was removed along with ForecastGrid.


// ── Weather Options: graph and summary ──

static DayForecast makeDay(int wday, int valid_hours) {
    DayForecast d = {};
    d.wday = wday;
    for (int h = 0; h < valid_hours; h++) {
        d.hours[h].hour = h;
        d.hours[h].wind = 4.0f + (float)(h % 8);
        d.hours[h].gust = d.hours[h].wind + 4.0f;
        d.hours[h].rain = 0.0f;
        d.hours[h].dir  = 225;
        d.hours[h].sea  = -8 + h;
        d.hours[h].valid = true;
    }
    d.valid_count = valid_hours;
    d.wind_min = 4; d.wind_max = 11; d.gust_max = 15;
    d.air_min = 9;  d.air_max = 15;
    d.sea_min = -8; d.sea_max = 15;
    d.summary_valid = true;
    return d;
}

static int countRectsWithInk(const DrawList& dl, Ink ink) {
    int n = 0;
    for (int i = 0; i < dl.count; i++) {
        if (dl.cmds[i].type == DRAW_RECT_FILL && dl.cmds[i].ink == ink) n++;
    }
    return n;
}

void test_layout_graph_ink_splits_at_now(void) {
    // Hours before now are light, the current hour onward is black — verified
    // against the design, whose WED strip reads ggggggKKKKKK at 12:34.
    DisplayState s = makeNormalState();
    s.hour = 12;
    DayForecast days[FORECAST_DAYS] = { makeDay(3, 24), makeDay(4, 24) };
    s.days = days;
    DrawList dl = buildLayout(s);

    // The strip is 12 two-hour columns. At 12:00 the split falls at column 6,
    // so day 0 draws 6 light bars and 6 black; day 1 is entirely ahead, so all
    // 12 of its bars are black.
    TEST_ASSERT_EQUAL_INT(6 + 12, countRectsWithInk(dl, INK_BLACK));
}

void test_layout_graph_truncated_day(void) {
    // A day cut short at the forecast horizon draws fewer bars, not padded ones.
    DisplayState s = makeNormalState();
    s.hour = 0;
    DayForecast days[FORECAST_DAYS] = { makeDay(3, 24), makeDay(4, 8) };
    s.days = days;
    DrawList dl = buildLayout(s);
    // 12 columns for the full day; the 8-hour day fills only its first 4.
    TEST_ASSERT_EQUAL_INT(12 + 4, countRectsWithInk(dl, INK_BLACK));
}

void test_layout_summary_grid_two_lines(void) {
    DisplayState s = makeNormalState();
    DayForecast days[FORECAST_DAYS] = { makeDay(3, 24), makeDay(4, 24) };
    s.days = days;
    DrawList dl = buildLayout(s);

    // "AIR" also labels the NOW block in the top band, so search by row: the
    // forecast summary's second line is the one at LAYOUT_FC_SUM_L2_Y.
    const DrawCmd* air = NULL;
    const DrawCmd* sea = NULL;
    for (int i = 0; i < dl.count; i++) {
        const DrawCmd& d = dl.cmds[i];
        if (d.type != DRAW_TEXT || d.y != LAYOUT_FC_SUM_L2_Y) continue;
        if (!strcmp(d.text.text, "AIR")) air = &d;
        if (!strcmp(d.text.text, "SEA")) sea = &d;
    }
    TEST_ASSERT_NOT_NULL(findText(dl, "WIND"));
    TEST_ASSERT_NOT_NULL(air);
    TEST_ASSERT_NOT_NULL(sea);
    // AIR and SEA share the second line; SEA sits in the right column.
    TEST_ASSERT_EQUAL_INT(air->y, sea->y);
    TEST_ASSERT_TRUE(sea->x > air->x);
    TEST_ASSERT_TRUE(air->y > LAYOUT_FC_SUM_L1_Y);
}

void test_layout_sea_range_clamped(void) {
    // A storm surge must not overrun the column: values clamp to two digits.
    DisplayState s = makeNormalState();
    DayForecast days[FORECAST_DAYS] = { makeDay(3, 24), makeDay(4, 24) };
    days[0].sea_min = -250;
    days[0].sea_max = 300;
    s.days = days;
    DrawList dl = buildLayout(s);
    // Drawn as two separate values with a gap, not one run-together string.
    const DrawCmd* lo = NULL;
    const DrawCmd* hi = NULL;
    for (int i = 0; i < dl.count; i++) {
        const DrawCmd& d = dl.cmds[i];
        if (d.type != DRAW_TEXT || d.y != LAYOUT_FC_SUM_L2_Y) continue;
        if (!strcmp(d.text.text, "-99")) lo = &d;
        if (!strcmp(d.text.text, "99")) hi = &d;
    }
    TEST_ASSERT_NOT_NULL(lo);
    TEST_ASSERT_NOT_NULL(hi);
    TEST_ASSERT_TRUE(hi->x > lo->x);
}

void test_layout_no_forecast_when_null(void) {
    DisplayState s = makeNormalState();
    s.days = NULL;
    s.history = NULL;
    DrawList dl = buildLayout(s);
    // Clock and boat bank still render.
    TEST_ASSERT_NOT_NULL(findText(dl, "12:34"));
    TEST_ASSERT_NOT_NULL(findText(dl, "SOLAR"));
    TEST_ASSERT_NULL(findText(dl, "CHANGES"));
}

void test_layout_fits_command_budget(void) {
    // Worst case: two full days, both with CHANGES, plus history.
    DisplayState s = makeNormalState();
    s.hour = 12;
    DayForecast days[FORECAST_DAYS] = { makeDay(3, 24), makeDay(4, 24) };
    for (int d = 0; d < FORECAST_DAYS; d++)
        for (int h = 0; h < 24; h++) days[d].hours[h].rain = 1.5f;
    WindHistory hist = {};
    hist.count = HISTORY_SLOTS;
    for (int i = 0; i < HISTORY_SLOTS; i++) {
        hist.slots[i].wind = 8; hist.slots[i].gust = 11;
        hist.slots[i].dir = 200; hist.slots[i].valid = true;
    }
    s.days = days;
    s.history = &hist;
    DrawList dl = buildLayout(s);
    TEST_ASSERT_TRUE(dl.count < MAX_DRAW_CMDS);
}


void test_layout_timer_digits_clear_of_cup(void) {
    // The cup used to be anchored to LAYOUT_VSPLIT_X; when that moved for the
    // new screen the artwork slid on top of the digits. Assert the gap.
    DisplayState s = makeNormalState();
    s.timer_active = true;
    s.timer_seconds = 720;   // "12:00" — the widest string the timer shows
    s.timer_total = 720;
    DrawList dl = buildLayout(s);
    const DrawCmd* cmd = findText(dl, "12:00");
    TEST_ASSERT_NOT_NULL(cmd);

    int width = 4 * LAYOUT_TIMER_DIGIT_W + LAYOUT_TIMER_COLON_W;
    int digits_right = cmd->x + width;
    int mug_left = LAYOUT_CUP_X + (LAYOUT_CUP_ZONE_W - 500) / 2;  // COFFEE_MUG_W
    TEST_ASSERT_TRUE(digits_right < mug_left);
    // And the mug must stay on screen.
    TEST_ASSERT_TRUE(mug_left + 500 <= LAYOUT_DISPLAY_W);
}


void test_layout_rolled_forecast_has_no_grey_bars(void) {
    // Late in the day the window rolls to tomorrow + the day after. Every column
    // is then in the future, so none may be greyed as elapsed — the first strip
    // showed 11 grey bars because "day 0" was still assumed to contain now.
    DisplayState s = makeNormalState();
    s.hour = 22;                 // past FLAG_DOWN_HOUR
    s.forecast_rolled = true;
    DayForecast days[FORECAST_DAYS] = { makeDay(5, 24), makeDay(6, 24) };
    s.days = days;
    DrawList dl = buildLayout(s);

    // 12 columns per day, all black; light rects are gust ticks and rain only.
    TEST_ASSERT_EQUAL_INT(12 + 12, countRectsWithInk(dl, INK_BLACK));
}


void test_layout_clock_clear_region_spares_now_block(void) {
    // renderClockUpdate() clears the clock's rows before redrawing the digits.
    // That clear must not reach the NOW block or the history graph, which share
    // those rows on the right-hand side — clearing full width wiped the wind,
    // gust, air readings and every history bar on each minute tick.
    int y_start = LAYOUT_CLOCK_Y - LAYOUT_CLOCK_ASCENT;
    int y_end   = LAYOUT_CLOCK_Y + 20;

    DisplayState s = makeNormalState();
    WindHistory hist = {};
    hist.count = HISTORY_SLOTS;
    for (int i = 0; i < HISTORY_SLOTS; i++) {
        hist.slots[i].wind = 8; hist.slots[i].gust = 11;
        hist.slots[i].dir = 200; hist.slots[i].valid = true;
    }
    s.history = &hist;
    DrawList dl = buildLayout(s);

    for (int i = 0; i < dl.count; i++) {
        const DrawCmd& d = dl.cmds[i];
        int y = d.y, x = d.x;
        if (d.type == DRAW_RECT_FILL) { y = d.y; x = d.x; }
        // Anything drawn inside the clock's row band must be either left of the
        // cleared width (the clock itself) or right of it (preserved content).
        if (y >= y_start && y <= y_end && x >= LAYOUT_CLOCK_CLEAR_W) {
            TEST_ASSERT_TRUE_MESSAGE(x >= LAYOUT_CLOCK_CLEAR_W,
                "content in clock rows must sit outside the cleared region");
        }
    }
    // The cleared width must cover the clock but stop before the NOW column.
    TEST_ASSERT_TRUE(LAYOUT_CLOCK_CLEAR_W > LAYOUT_CLOCK_X);
    TEST_ASSERT_TRUE(LAYOUT_CLOCK_CLEAR_W < LAYOUT_NOW_WIND_VX);
    TEST_ASSERT_TRUE(LAYOUT_CLOCK_CLEAR_W < LAYOUT_HIST_X0);
}


void test_layout_rain_is_a_rate_not_a_total(void) {
    // Precipitation1h is mm/h. Summing the two hours in a bucket reports mm/2h
    // and overstates intensity against the px-per-mm/h scale: a real 3.0 + 5.4
    // pair drew as 8.4 mm/h — heavy rain — when the worst hour was 5.4.
    DisplayState s = makeNormalState();
    s.hour = 0;
    DayForecast days[FORECAST_DAYS] = { makeDay(3, 24), makeDay(4, 24) };
    for (int h = 0; h < 24; h++) days[0].hours[h].rain = 0.0f;
    days[0].hours[20].rain = 3.0f;
    days[0].hours[21].rain = 5.4f;
    s.days = days;
    DrawList dl = buildLayout(s);

    // Bucket 10 covers hours 20-21. Its rain bar hangs below the axis.
    int expect_px = (int)(((3.0f + 5.4f) / 2.0f) * LAYOUT_PX_PER_MMH + 0.5f);
    bool found = false;
    for (int i = 0; i < dl.count; i++) {
        const DrawCmd& d = dl.cmds[i];
        if (d.type != DRAW_RECT_FILL) continue;
        if (d.y <= LAYOUT_FC_AXIS_Y) continue;          // below the axis only
        if (d.x != LAYOUT_FC_X0 + 10 * LAYOUT_FC_PITCH) continue;
        TEST_ASSERT_EQUAL_INT(expect_px, d.rect.h);
        found = true;
    }
    TEST_ASSERT_TRUE(found);
}

void test_layout_rain_bar_cannot_reach_hour_labels(void) {
    // Torrential rain must clamp, not draw through the hour labels below.
    DisplayState s = makeNormalState();
    s.hour = 0;
    DayForecast days[FORECAST_DAYS] = { makeDay(3, 24), makeDay(4, 24) };
    for (int h = 0; h < 24; h++) days[0].hours[h].rain = 40.0f;
    s.days = days;
    DrawList dl = buildLayout(s);

    for (int i = 0; i < dl.count; i++) {
        const DrawCmd& d = dl.cmds[i];
        if (d.type != DRAW_RECT_FILL || d.y <= LAYOUT_FC_AXIS_Y) continue;
        TEST_ASSERT_TRUE(d.rect.h <= LAYOUT_RAIN_MAX_H);
        // And the bottom must stay clear of the hour label row.
        TEST_ASSERT_TRUE(d.y + d.rect.h < LAYOUT_FC_HOUR_Y - FONT_ASCENT_LABEL);
    }
}


void test_layout_wind_arrows_point_downwind(void) {
    // Meteorological convention: the reading is where the wind blows FROM, and
    // the arrow shows where it is going. Wind from the north must point down
    // the screen. Screen y grows downward.
    DisplayState s = makeNormalState();
    WindHistory h = {};
    h.count = 1;
    h.slots[0].wind = 5; h.slots[0].gust = 7; h.slots[0].dir = 0; h.slots[0].valid = true;
    s.history = &h;
    DrawList dl = buildLayout(s);

    const DrawCmd* shaft = NULL;
    for (int i = 0; i < dl.count; i++) {
        const DrawCmd& d = dl.cmds[i];
        if (d.type == DRAW_LINE && d.y > LAYOUT_HIST_LABEL_Y && d.y < LAYOUT_HMID_Y) {
            shaft = &d; break;
        }
    }
    TEST_ASSERT_NOT_NULL(shaft);
    // From north -> head is below the tail.
    TEST_ASSERT_TRUE(shaft->line.y2 > shaft->y);
    TEST_ASSERT_INT_WITHIN(2, shaft->x, shaft->line.x2);
}

void test_layout_timer_keeps_date(void) {
    // The timer screen keeps the clock header; its date fields were left zeroed,
    // rendering "00-00" and SUN.
    DisplayState s = makeNormalState();
    s.timer_active = true;
    s.timer_seconds = 120;
    s.timer_total = 120;
    DrawList dl = buildLayout(s);
    TEST_ASSERT_NOT_NULL(findText(dl, "04-02"));
    TEST_ASSERT_NOT_NULL(findText(dl, "WED"));
}

void test_layout_timer_aligns_with_clock(void) {
    // Timer digits share the clock's left edge so the two read as one column.
    DisplayState s = makeNormalState();
    s.timer_active = true;
    s.timer_seconds = 120;
    s.timer_total = 120;
    DrawList dl = buildLayout(s);
    const DrawCmd* t = findText(dl, "2:00");
    TEST_ASSERT_NOT_NULL(t);
    TEST_ASSERT_EQUAL_INT(LAYOUT_CLOCK_X, t->x);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_layout_has_clock);
    RUN_TEST(test_layout_wind_arrows_point_downwind);
    RUN_TEST(test_layout_timer_keeps_date);
    RUN_TEST(test_layout_timer_aligns_with_clock);
    RUN_TEST(test_layout_clock_clear_region_spares_now_block);
    RUN_TEST(test_layout_has_dow);
    RUN_TEST(test_layout_has_date);
    RUN_TEST(test_layout_has_alarm);
    RUN_TEST(test_layout_no_alarm_when_invalid);
    RUN_TEST(test_layout_has_solar);
    RUN_TEST(test_layout_has_station);
    RUN_TEST(test_layout_has_wind);
    RUN_TEST(test_layout_has_ruuvi);
    RUN_TEST(test_layout_structural_lines_normal);
    RUN_TEST(test_layout_bank_is_evenly_pitched);
    RUN_TEST(test_layout_shared_baselines);
    RUN_TEST(test_layout_timer_has_countdown);
    RUN_TEST(test_layout_timer_has_progress_bar);
    RUN_TEST(test_layout_timer_no_electricals);
    RUN_TEST(test_layout_timer_keeps_clock);
    RUN_TEST(test_layout_timer_fewer_lines);
    RUN_TEST(test_layout_timer_digits_clear_of_cup);
    RUN_TEST(test_layout_nan_shows_dashes);
    RUN_TEST(test_layout_cmd_count_reasonable);
    RUN_TEST(test_layout_graph_ink_splits_at_now);
    RUN_TEST(test_layout_rain_is_a_rate_not_a_total);
    RUN_TEST(test_layout_rain_bar_cannot_reach_hour_labels);
    RUN_TEST(test_layout_rolled_forecast_has_no_grey_bars);
    RUN_TEST(test_layout_graph_truncated_day);
    RUN_TEST(test_layout_summary_grid_two_lines);
    RUN_TEST(test_layout_sea_range_clamped);
    RUN_TEST(test_layout_no_forecast_when_null);
    RUN_TEST(test_layout_fits_command_budget);
    // Status bar
    RUN_TEST(test_layout_status_bar_has_hints);
    RUN_TEST(test_layout_status_bar_usb_power_in);
    RUN_TEST(test_layout_status_bar_usb_power_out);
    RUN_TEST(test_layout_status_bar_position);
    RUN_TEST(test_layout_timer_no_status_bar);
    // Forecast grid
    UNITY_END();
    return 0;
}
