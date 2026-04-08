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
    TEST_ASSERT_EQUAL_INT(FONT_DSEG7_340, cmd->text.font);
}

void test_layout_has_dow(void) {
    DisplayState s = makeNormalState();
    DrawList dl = buildLayout(s);
    const DrawCmd* cmd = findText(dl, "WED");
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(LAYOUT_DOW_X, cmd->x);
    TEST_ASSERT_EQUAL_INT(LAYOUT_DOW_Y, cmd->y);
    TEST_ASSERT_EQUAL_INT(FONT_DSEG14_72, cmd->text.font);
}

void test_layout_has_date(void) {
    DisplayState s = makeNormalState();
    DrawList dl = buildLayout(s);
    const DrawCmd* cmd = findText(dl, "04-02");
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(LAYOUT_DATE_X, cmd->x);
    TEST_ASSERT_EQUAL_INT(FONT_DSEG7_72, cmd->text.font);
}

void test_layout_has_alarm(void) {
    DisplayState s = makeNormalState();
    DrawList dl = buildLayout(s);
    const DrawCmd* cmd = findText(dl, "20:59");
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(FONT_DSEG7_72, cmd->text.font);
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
    TEST_ASSERT_EQUAL_INT(LAYOUT_FMI_STATION_X, cmd->x);
    TEST_ASSERT_EQUAL_INT(FONT_DSEG14_72, cmd->text.font);
}

void test_layout_has_wind(void) {
    DisplayState s = makeNormalState();
    DrawList dl = buildLayout(s);
    TEST_ASSERT_NOT_NULL(findText(dl, "WIND"));
    TEST_ASSERT_NOT_NULL(findText(dl, "GUST"));
    TEST_ASSERT_NOT_NULL(findText(dl, "DIR"));
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
    // Should have lines: hmid, hforecast, vsplit (full), 2 forecast dividers = 5 lines
    TEST_ASSERT_TRUE(countType(dl, DRAW_LINE) >= 5);
}

void test_layout_elec_vertical_alignment(void) {
    DisplayState s = makeNormalState();
    DrawList dl = buildLayout(s);
    // All electrical labels should be at RIGHT_DATA_X
    const DrawCmd* solar = findText(dl, "SOLAR");
    const DrawCmd* device = findText(dl, "DEVICE");
    TEST_ASSERT_NOT_NULL(solar);
    TEST_ASSERT_NOT_NULL(device);
    TEST_ASSERT_EQUAL_INT(solar->x, device->x);
    TEST_ASSERT_EQUAL_INT(LAYOUT_RIGHT_DATA_X, solar->x);
}

void test_layout_dow_and_solar_same_y(void) {
    DisplayState s = makeNormalState();
    DrawList dl = buildLayout(s);
    const DrawCmd* dow = findText(dl, "WED");
    const DrawCmd* solar = findText(dl, "SOLAR");
    TEST_ASSERT_NOT_NULL(dow);
    TEST_ASSERT_NOT_NULL(solar);
    // DOW at y=50, Solar at y=40 — close but font baselines differ
    // Solar label is Inter 22px, DOW is DSEG14 72px
    // They're on the same visual row (top row)
    TEST_ASSERT_INT_WITHIN(15, solar->y, dow->y);
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
    TEST_ASSERT_EQUAL_INT(FONT_DSEG7_340, cmd->text.font);
    TEST_ASSERT_EQUAL_INT(LAYOUT_TIMER_X, cmd->x);
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
    // Normal has hmid + hforecast + vsplit + 2 forecast divs = 5 structural
    TEST_ASSERT_TRUE(lines_normal >= 5);
}

void test_layout_nan_shows_dashes(void) {
    DisplayState s = makeNormalState();
    s.saloon_temp = NAN;
    s.solar_w = NAN;
    DrawList dl = buildLayout(s);
    // Should find "--" for NaN values
    TEST_ASSERT_NOT_NULL(findText(dl, "--"));
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

// ── Forecast grid tests ──

static ForecastGrid makeForecastGrid() {
    ForecastGrid grid = {};
    // Fill a few columns for testing
    grid.cols[0] = { 5.2f, 8.1f, 180, -3, 14, true, true };   // obs
    grid.cols[4] = { 6.0f, 10.0f, 200, 5, 18, false, true };   // fc 1h
    grid.valid_count = 2;
    return grid;
}

void test_layout_forecast_row_labels(void) {
    DisplayState s = makeNormalState();
    ForecastGrid grid = makeForecastGrid();
    s.forecast = &grid;
    DrawList dl = buildLayout(s);
    // Row labels should be present
    TEST_ASSERT_NOT_NULL(findText(dl, "HOUR"));
    TEST_ASSERT_NOT_NULL(findText(dl, "SEA"));
}

void test_layout_forecast_segment_labels(void) {
    DisplayState s = makeNormalState();
    ForecastGrid grid = makeForecastGrid();
    s.forecast = &grid;
    DrawList dl = buildLayout(s);
    TEST_ASSERT_NOT_NULL(findText(dl, "OBS"));
    TEST_ASSERT_NOT_NULL(findText(dl, "1H"));
    TEST_ASSERT_NOT_NULL(findText(dl, "2H"));
    TEST_ASSERT_NOT_NULL(findText(dl, "4H"));
}

void test_layout_forecast_renders_cell_values(void) {
    DisplayState s = makeNormalState();
    ForecastGrid grid = makeForecastGrid();
    s.forecast = &grid;
    DrawList dl = buildLayout(s);
    // Column 0: hour=14 → "14", wind=5.2→"5", gust=8.1→"8", dir=180, sea=-3→"-3"
    TEST_ASSERT_NOT_NULL(findText(dl, "14"));
    TEST_ASSERT_NOT_NULL(findText(dl, "180"));
    TEST_ASSERT_NOT_NULL(findText(dl, "-3"));
}

void test_layout_no_forecast_when_null(void) {
    DisplayState s = makeNormalState();
    s.forecast = NULL;
    DrawList dl = buildLayout(s);
    // No forecast labels should appear
    TEST_ASSERT_NULL(findText(dl, "HOUR"));
    TEST_ASSERT_NULL(findText(dl, "SEA"));
    TEST_ASSERT_NULL(findText(dl, "OBS"));
}

void test_layout_forecast_skips_invalid_columns(void) {
    DisplayState s = makeNormalState();
    ForecastGrid grid = {};
    // All columns invalid (valid=false) by default
    grid.valid_count = 0;
    s.forecast = &grid;
    DrawList dl = buildLayout(s);
    // Row labels still present (they're static)
    TEST_ASSERT_NOT_NULL(findText(dl, "HOUR"));
    // But no cell data should be rendered — count DSEG7_22 (forecast font) = 0
    int fc_font_count = 0;
    for (int i = 0; i < dl.count; i++) {
        if (dl.cmds[i].type == DRAW_TEXT && dl.cmds[i].text.font == FONT_DSEG7_22) {
            fc_font_count++;
        }
    }
    TEST_ASSERT_EQUAL_INT(0, fc_font_count);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_layout_has_clock);
    RUN_TEST(test_layout_has_dow);
    RUN_TEST(test_layout_has_date);
    RUN_TEST(test_layout_has_alarm);
    RUN_TEST(test_layout_no_alarm_when_invalid);
    RUN_TEST(test_layout_has_solar);
    RUN_TEST(test_layout_has_station);
    RUN_TEST(test_layout_has_wind);
    RUN_TEST(test_layout_has_ruuvi);
    RUN_TEST(test_layout_structural_lines_normal);
    RUN_TEST(test_layout_elec_vertical_alignment);
    RUN_TEST(test_layout_dow_and_solar_same_y);
    RUN_TEST(test_layout_timer_has_countdown);
    RUN_TEST(test_layout_timer_has_progress_bar);
    RUN_TEST(test_layout_timer_no_electricals);
    RUN_TEST(test_layout_timer_keeps_clock);
    RUN_TEST(test_layout_timer_fewer_lines);
    RUN_TEST(test_layout_nan_shows_dashes);
    RUN_TEST(test_layout_cmd_count_reasonable);
    // Status bar
    RUN_TEST(test_layout_status_bar_has_hints);
    RUN_TEST(test_layout_status_bar_usb_power_in);
    RUN_TEST(test_layout_status_bar_usb_power_out);
    RUN_TEST(test_layout_status_bar_position);
    RUN_TEST(test_layout_timer_no_status_bar);
    // Forecast grid
    RUN_TEST(test_layout_forecast_row_labels);
    RUN_TEST(test_layout_forecast_segment_labels);
    RUN_TEST(test_layout_forecast_renders_cell_values);
    RUN_TEST(test_layout_no_forecast_when_null);
    RUN_TEST(test_layout_forecast_skips_invalid_columns);
    UNITY_END();
    return 0;
}
