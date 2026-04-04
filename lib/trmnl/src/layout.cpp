#include "layout.h"
#include "format.h"
#include "trmnl_log.h"
#include <cstring>
#include <cstdio>
#include <cmath>

// Static text buffers (persist until next buildLayout call)
static char buf_time[8];
static char buf_date[8];
static char buf_dow[4];
static char buf_alarm[8];
static char buf_solar[8];
static char buf_charger[8];
static char buf_battery[8];
static char buf_soc[8];
static char buf_engine[8];
static char buf_device[8];
static char buf_wind[8];
static char buf_gust[8];
static char buf_dir[8];
static char buf_saloon_t[8];
static char buf_saloon_h[8];
static char buf_icebox[8];
static char buf_timer[8];

static void addText(DrawList& dl, int x, int y, const char* text, FontId font) {
    if (dl.count >= MAX_DRAW_CMDS) return;
    DrawCmd& c = dl.cmds[dl.count++];
    c.type = DRAW_TEXT;
    c.x = x; c.y = y;
    c.text.text = text;
    c.text.font = font;
}

static void addLine(DrawList& dl, int x1, int y1, int x2, int y2, int thickness) {
    if (dl.count >= MAX_DRAW_CMDS) return;
    DrawCmd& c = dl.cmds[dl.count++];
    c.type = DRAW_LINE;
    c.x = x1; c.y = y1;
    c.line.x2 = x2; c.line.y2 = y2;
    c.line.thickness = thickness;
}

static void addRect(DrawList& dl, int x, int y, int w, int h) {
    if (dl.count >= MAX_DRAW_CMDS) return;
    DrawCmd& c = dl.cmds[dl.count++];
    c.type = DRAW_RECT_FILL;
    c.x = x; c.y = y;
    c.rect.w = w; c.rect.h = h;
}

// Add a data row: LABEL (Inter 22px) + VALUE (DSEG7 72px) + UNIT (Inter 22px)
static void addDataRow(DrawList& dl, int x, int y,
                       const char* label, const char* value, const char* unit) {
    if (label && label[0]) {
        addText(dl, x, y, label, FONT_UBUNTU_22);
    }
    addText(dl, x + LAYOUT_LABEL_W, y, value, FONT_DSEG7_72);
    if (unit && unit[0]) {
        addText(dl, x + LAYOUT_LABEL_W + LAYOUT_VALUE_W + 8, y, unit, FONT_UBUNTU_22);
    }
}

static void buildNormalStructure(DrawList& dl) {
    // Horizontal: mid separator (full width)
    addLine(dl, 0, LAYOUT_HMID_Y, LAYOUT_DISPLAY_W, LAYOUT_HMID_Y, 2);
    // Horizontal: above forecast (full width)
    addLine(dl, 0, LAYOUT_HFORECAST_Y, LAYOUT_DISPLAY_W, LAYOUT_HFORECAST_Y, 2);
    // Vertical: left/right split (top to forecast)
    addLine(dl, LAYOUT_VSPLIT_X, 0, LAYOUT_VSPLIT_X, LAYOUT_HFORECAST_Y, 2);
    // Forecast column dividers
    addLine(dl, LAYOUT_FC_DIV1_X, LAYOUT_HFORECAST_Y, LAYOUT_FC_DIV1_X, LAYOUT_DISPLAY_H, 2);
    addLine(dl, LAYOUT_VSPLIT_X, LAYOUT_HFORECAST_Y, LAYOUT_VSPLIT_X, LAYOUT_DISPLAY_H, 2);
}

static void buildTimerStructure(DrawList& dl) {
    // Horizontal: mid separator (full width) — stays in timer mode
    addLine(dl, 0, LAYOUT_HMID_Y, LAYOUT_DISPLAY_W, LAYOUT_HMID_Y, 2);
    // Vertical: only above mid line
    addLine(dl, LAYOUT_VSPLIT_X, 0, LAYOUT_VSPLIT_X, LAYOUT_HMID_Y, 2);
}

static void buildClockArea(DrawList& dl, const DisplayState& s) {
    // Day of week
    formatDow(buf_dow, sizeof(buf_dow), s.wday);
    addText(dl, LAYOUT_DOW_X, LAYOUT_DOW_Y, buf_dow, FONT_DSEG14_72);

    // Date (data-row position, no label)
    formatDate(buf_date, sizeof(buf_date), s.day, s.month);
    addText(dl, LAYOUT_DATE_X, LAYOUT_DATE_Y, buf_date, FONT_DSEG7_72);

    // Clock digits
    formatTime(buf_time, sizeof(buf_time), s.hour, s.minute);
    addText(dl, LAYOUT_CLOCK_X, LAYOUT_CLOCK_Y, buf_time, FONT_DSEG7_340);

    // Alarm
    if (s.alarm_valid) {
        formatTime(buf_alarm, sizeof(buf_alarm), s.alarm_hour, s.alarm_minute);
        addDataRow(dl, LAYOUT_LEFT_DATA_X, LAYOUT_ALARM_Y, "ALARM", buf_alarm, NULL);
    }
}

static void buildElectricals(DrawList& dl, const DisplayState& s) {
    int y = LAYOUT_ELEC_Y0;

    formatOrDash(buf_solar, sizeof(buf_solar), s.solar_w, formatTemp);
    if (!std::isnan(s.solar_w)) formatInt(buf_solar, sizeof(buf_solar), (int)s.solar_w);
    addDataRow(dl, LAYOUT_RIGHT_DATA_X, y, "SOLAR", buf_solar, "W");
    y += LAYOUT_ELEC_DY;

    formatOrDash(buf_charger, sizeof(buf_charger), s.charger_w, formatTemp);
    if (!std::isnan(s.charger_w)) formatInt(buf_charger, sizeof(buf_charger), (int)s.charger_w);
    addDataRow(dl, LAYOUT_RIGHT_DATA_X, y, "CHARGER", buf_charger, "W");
    y += LAYOUT_ELEC_DY;

    formatOrDash(buf_battery, sizeof(buf_battery), s.battery_w, formatTemp);
    if (!std::isnan(s.battery_w)) formatInt(buf_battery, sizeof(buf_battery), (int)s.battery_w);
    addDataRow(dl, LAYOUT_RIGHT_DATA_X, y, "BATTERY", buf_battery, "W");
    y += LAYOUT_ELEC_DY;

    formatPct(buf_soc, sizeof(buf_soc), s.soc_pct);
    addDataRow(dl, LAYOUT_RIGHT_DATA_X, y, "SOC", buf_soc, "%");
    y += LAYOUT_ELEC_DY;

    formatFloat1(buf_engine, sizeof(buf_engine), s.engine_v);
    addDataRow(dl, LAYOUT_RIGHT_DATA_X, y, "ENGINE", buf_engine, "V");
    y += LAYOUT_ELEC_DY;

    formatPct(buf_device, sizeof(buf_device), s.device_pct);
    addDataRow(dl, LAYOUT_RIGHT_DATA_X, y, "DEVICE", buf_device, "%");
}

static void buildFmi(DrawList& dl, const DisplayState& s) {
    // Station name
    if (s.station_name) {
        addText(dl, LAYOUT_FMI_STATION_X, LAYOUT_FMI_STATION_Y, s.station_name, FONT_DSEG14_72);
    }

    int y = LAYOUT_FMI_Y0;

    formatOrDash(buf_wind, sizeof(buf_wind), s.wind_speed, formatTemp);
    if (!std::isnan(s.wind_speed)) formatInt(buf_wind, sizeof(buf_wind), (int)s.wind_speed);
    addDataRow(dl, LAYOUT_LEFT_DATA_X, y, "WIND", buf_wind, "m/s");
    y += LAYOUT_FMI_DY;

    formatOrDash(buf_gust, sizeof(buf_gust), s.wind_gust, formatTemp);
    if (!std::isnan(s.wind_gust)) formatInt(buf_gust, sizeof(buf_gust), (int)s.wind_gust);
    addDataRow(dl, LAYOUT_LEFT_DATA_X, y, "GUST", buf_gust, "m/s");
    y += LAYOUT_FMI_DY;

    formatInt(buf_dir, sizeof(buf_dir), s.wind_dir);
    addDataRow(dl, LAYOUT_LEFT_DATA_X, y, "DIR", buf_dir, "\xC2\xB0"); // °
}

static void buildRuuvi(DrawList& dl, const DisplayState& s) {
    int y = LAYOUT_RUUVI_Y0;

    formatOrDash(buf_saloon_t, sizeof(buf_saloon_t), s.saloon_temp, formatTemp);
    addDataRow(dl, LAYOUT_RIGHT_DATA_X, y, "SALOON", buf_saloon_t, "\xC2\xB0""C");
    y += LAYOUT_RUUVI_DY;

    formatOrDash(buf_saloon_h, sizeof(buf_saloon_h), s.saloon_humidity, formatTemp);
    if (!std::isnan(s.saloon_humidity)) formatPct(buf_saloon_h, sizeof(buf_saloon_h), (int)s.saloon_humidity);
    addDataRow(dl, LAYOUT_RIGHT_DATA_X, y, "", buf_saloon_h, "%");
    y += LAYOUT_RUUVI_DY;

    formatOrDash(buf_icebox, sizeof(buf_icebox), s.icebox_temp, formatTemp);
    addDataRow(dl, LAYOUT_RIGHT_DATA_X, y, "ICEBOX", buf_icebox, "\xC2\xB0""C");
}

static void buildTimer(DrawList& dl, const DisplayState& s) {
    // Timer digits — centred in the y=680→1404 zone
    formatTimer(buf_timer, sizeof(buf_timer), s.timer_seconds);
    addText(dl, LAYOUT_TIMER_X, LAYOUT_TIMER_Y, buf_timer, FONT_DSEG7_340);

    // Progress bar
    float progress = 0;
    if (s.timer_total > 0) {
        progress = 1.0f - (float)s.timer_seconds / s.timer_total;
        if (progress < 0) progress = 0;
        if (progress > 1) progress = 1;
    }
    int fill_w = (int)(LAYOUT_TIMER_BAR_W * progress);

    // Bar outline (as 4 lines)
    addLine(dl, LAYOUT_TIMER_BAR_X, LAYOUT_TIMER_BAR_Y,
            LAYOUT_TIMER_BAR_X + LAYOUT_TIMER_BAR_W, LAYOUT_TIMER_BAR_Y, 2);
    addLine(dl, LAYOUT_TIMER_BAR_X, LAYOUT_TIMER_BAR_Y + LAYOUT_TIMER_BAR_H,
            LAYOUT_TIMER_BAR_X + LAYOUT_TIMER_BAR_W, LAYOUT_TIMER_BAR_Y + LAYOUT_TIMER_BAR_H, 2);
    addLine(dl, LAYOUT_TIMER_BAR_X, LAYOUT_TIMER_BAR_Y,
            LAYOUT_TIMER_BAR_X, LAYOUT_TIMER_BAR_Y + LAYOUT_TIMER_BAR_H, 2);
    addLine(dl, LAYOUT_TIMER_BAR_X + LAYOUT_TIMER_BAR_W, LAYOUT_TIMER_BAR_Y,
            LAYOUT_TIMER_BAR_X + LAYOUT_TIMER_BAR_W, LAYOUT_TIMER_BAR_Y + LAYOUT_TIMER_BAR_H, 2);

    // Fill
    if (fill_w > 0) {
        addRect(dl, LAYOUT_TIMER_BAR_X + 2, LAYOUT_TIMER_BAR_Y + 2,
                fill_w - 4, LAYOUT_TIMER_BAR_H - 4);
    }
}

// 24 column center x-positions (from layout prototype)
static const int FC_COL_X[FORECAST_COLS] = {
    117, 192, 267, 341,     // OBS (4 cols)
    416, 491, 565, 640,     // FC 1H (4 cols)
    715, 789, 864, 939, 1013, 1088, 1163, 1237,  // FC 2H (8 cols)
    1312, 1387, 1461, 1536, 1611, 1685, 1760, 1835  // FC 4H (8 cols)
};

// Static buffers for forecast cell values — one per cell (24 cols × 5 rows)
static char fc_cells[FORECAST_COLS][5][8];

static void buildForecast(DrawList& dl, const ForecastGrid& grid) {
    // Row labels (left edge)
    addText(dl, LAYOUT_FC_LABEL_X, LAYOUT_FC_ROW_HR_Y,   "HOUR", FONT_UBUNTU_22);
    addText(dl, LAYOUT_FC_LABEL_X, LAYOUT_FC_ROW_WIND_Y, "WIND", FONT_UBUNTU_22);
    addText(dl, LAYOUT_FC_LABEL_X, LAYOUT_FC_ROW_GUST_Y, "GUST", FONT_UBUNTU_22);
    addText(dl, LAYOUT_FC_LABEL_X, LAYOUT_FC_ROW_DIR_Y,  "DIR",  FONT_UBUNTU_22);
    addText(dl, LAYOUT_FC_LABEL_X, LAYOUT_FC_ROW_SEA_Y,  "SEA",  FONT_UBUNTU_22);

    // Segment labels
    addText(dl, 190, LAYOUT_FC_SEG_Y, "OBS", FONT_UBUNTU_22);
    addText(dl, 530, LAYOUT_FC_SEG_Y, "1H",  FONT_UBUNTU_22);
    addText(dl, 978, LAYOUT_FC_SEG_Y, "2H",  FONT_UBUNTU_22);
    addText(dl, 1576, LAYOUT_FC_SEG_Y, "4H", FONT_UBUNTU_22);

    // Column values
    for (int i = 0; i < FORECAST_COLS; i++) {
        const ForecastColumn& col = grid.cols[i];
        if (!col.valid) continue;

        int x = FC_COL_X[i];

        // Hour
        snprintf(fc_cells[i][0], 8, "%02d", col.hour);
        addText(dl, x, LAYOUT_FC_ROW_HR_Y, fc_cells[i][0], FONT_DSEG7_22);

        // Wind (integer m/s)
        snprintf(fc_cells[i][1], 8, "%d", (int)(col.wind + 0.5f));
        addText(dl, x, LAYOUT_FC_ROW_WIND_Y, fc_cells[i][1], FONT_DSEG7_22);

        // Gust
        snprintf(fc_cells[i][2], 8, "%d", (int)(col.gust + 0.5f));
        addText(dl, x, LAYOUT_FC_ROW_GUST_Y, fc_cells[i][2], FONT_DSEG7_22);

        // Direction
        snprintf(fc_cells[i][3], 8, "%d", col.dir);
        addText(dl, x, LAYOUT_FC_ROW_DIR_Y, fc_cells[i][3], FONT_DSEG7_22);

        // Sea level
        formatSea(fc_cells[i][4], 8, col.sea);
        addText(dl, x, LAYOUT_FC_ROW_SEA_Y, fc_cells[i][4], FONT_DSEG7_22);
    }
}

// ── Coffee cup pixel art (timer decoration, 3-frame steam animation) ──
//
// Designed for ~300px wide × 350px tall cup body + handle,
// with ~200px of steam above. Total bounding box ~300×550.
// Coordinates are relative to (base_x, base_y) = top-left of area.
// The cup is centered horizontally in the 597px-wide region.
//
// Style: bold, utilitarian silhouette — nautical chart symbol aesthetic.

void buildCoffeeCup(DrawList& dl, int bx, int by, int frame) {
    // Center the ~300px cup in the 597px-wide area
    const int cx = bx + 250;   // cup body center x (shifted left to make room for handle)
    const int cup_top = by + 200;  // top of cup body (steam goes above)

    // ── Cup body (trapezoidal mug, built from filled rects) ──
    // Slightly tapered: wider at top, narrower at bottom.
    // Top width ~220px, bottom width ~180px, height ~260px.

    const int cup_w_top = 220;
    const int cup_w_bot = 180;
    const int cup_h = 260;
    const int wall = 16;  // wall thickness

    // Build the cup as a series of horizontal slices (filled rects)
    // for the taper. We'll do it in 4 bands for efficiency.
    const int band_h = cup_h / 4;  // 65px per band

    for (int i = 0; i < 4; i++) {
        int t = i;  // 0=top, 3=bottom
        float frac = (float)t / 3.0f;
        int w = cup_w_top - (int)((cup_w_top - cup_w_bot) * frac);
        int next_frac_w = cup_w_top - (int)((cup_w_top - cup_w_bot) * ((float)(t+1) / 3.0f));
        if (i == 3) next_frac_w = cup_w_bot;

        int x_left = cx - w / 2;
        int y_top = cup_top + i * band_h;

        // Left wall
        int next_x_left = cx - next_frac_w / 2;
        int left_x = (x_left < next_x_left) ? x_left : next_x_left;
        int left_w = wall + ((x_left > next_x_left) ? (x_left - next_x_left) : (next_x_left - x_left));
        addRect(dl, left_x, y_top, left_w, band_h);

        // Right wall
        int x_right = cx + w / 2 - wall;
        int next_x_right = cx + next_frac_w / 2 - wall;
        int right_x = (x_right < next_x_right) ? next_x_right : x_right;
        addRect(dl, right_x, y_top, left_w, band_h);
    }

    // Top rim (thick horizontal bar across full top width)
    addRect(dl, cx - cup_w_top / 2, cup_top, cup_w_top, wall);

    // Bottom (thick horizontal bar across bottom width)
    addRect(dl, cx - cup_w_bot / 2, cup_top + cup_h - wall, cup_w_bot, wall + 4);

    // ── Handle (right side, D-shaped, built from thick lines) ──
    const int handle_thick = 14;
    const int hx = cx + cup_w_top / 2;  // attach point x (right edge of cup)
    const int hy1 = cup_top + 50;       // top attachment
    const int hy2 = cup_top + 200;      // bottom attachment
    const int h_extend = 60;            // how far right the handle goes

    // Top horizontal
    addLine(dl, hx, hy1, hx + h_extend, hy1, handle_thick);
    // Right vertical
    addLine(dl, hx + h_extend, hy1, hx + h_extend, hy2, handle_thick);
    // Bottom horizontal
    addLine(dl, hx, hy2, hx + h_extend, hy2, handle_thick);

    // ── Saucer / base plate ──
    const int saucer_w = 280;
    const int saucer_h = 14;
    const int saucer_y = cup_top + cup_h + 6;
    addRect(dl, cx - saucer_w / 2, saucer_y, saucer_w, saucer_h);

    // ── Steam (3 variants, wavy vertical lines above the cup) ──
    // Three steam columns, each a series of short line segments
    // making a sine-like wave. The phase shifts per frame.
    const int steam_h = 170;       // total steam height
    const int steam_base = cup_top - 15;  // just above rim
    const int steam_thick = 8;
    const int n_seg = 6;           // segments per steam line
    const int seg_h = steam_h / n_seg;
    const int amplitude = 18;      // wave amplitude in pixels

    // Three steam column center positions
    const int steam_cx[3] = { cx - 55, cx, cx + 55 };
    // Phase offset per column (in segments) to stagger the waves
    const int col_phase[3] = { 0, 2, 4 };

    for (int col = 0; col < 3; col++) {
        int scx = steam_cx[col];
        int phase = col_phase[col] + frame * 2;  // shift by 2 segments per frame

        for (int s = 0; s < n_seg; s++) {
            int y1 = steam_base - s * seg_h;
            int y2 = steam_base - (s + 1) * seg_h;
            // Alternating left-right offsets based on segment + phase
            int dir1 = ((s + phase) % 2 == 0) ? 1 : -1;
            int dir2 = ((s + 1 + phase) % 2 == 0) ? 1 : -1;
            int x1 = scx + dir1 * amplitude;
            int x2 = scx + dir2 * amplitude;

            // Fade: thinner at top
            int thick = steam_thick - (s * steam_thick) / (n_seg + 2);
            if (thick < 3) thick = 3;

            addLine(dl, x1, y1, x2, y2, thick);
        }
    }
}

DrawList buildLayout(const DisplayState& state) {
    DrawList dl = {};

    // Clock area — always rendered
    buildClockArea(dl, state);

    if (state.timer_active) {
        buildTimerStructure(dl);
        buildTimer(dl, state);
        buildCoffeeCup(dl, LAYOUT_VSPLIT_X, 0, state.timer_frame);
        Log_info("Layout: timer mode, %ds remaining, frame %d, %d draw cmds",
                 state.timer_seconds, state.timer_frame, dl.count);
    } else {
        buildNormalStructure(dl);
        buildElectricals(dl, state);
        buildFmi(dl, state);
        buildRuuvi(dl, state);
        if (state.forecast) {
            buildForecast(dl, *state.forecast);
        }
        Log_info("Layout: normal mode, %d draw cmds", dl.count);
    }

    if (dl.count >= MAX_DRAW_CMDS) {
        Log_error("Layout: draw cmd limit reached (%d/%d)!", dl.count, MAX_DRAW_CMDS);
    }

    return dl;
}
