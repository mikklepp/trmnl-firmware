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
static char buf_ac[8];
static char buf_house[8];
static char buf_battery[8];
static char buf_engine[8];
static char buf_device[8];
static char buf_wind[8];
static char buf_gust[8];
static char buf_dir[8];
static char buf_saloon_t[8];
static char buf_saloon_h[8];
static char buf_icebox[8];
static char buf_timer[8];

static void addText(DrawList& dl, int x, int y, const char* text, FontId font,
                    Ink ink = INK_BLACK) {
    if (dl.count >= MAX_DRAW_CMDS) return;
    DrawCmd& c = dl.cmds[dl.count++];
    c.type = DRAW_TEXT;
    c.x = x; c.y = y;
    c.ink = ink;
    c.text.text = text;
    c.text.font = font;
}

static void addLine(DrawList& dl, int x1, int y1, int x2, int y2, int thickness,
                    Ink ink = INK_BLACK) {
    if (dl.count >= MAX_DRAW_CMDS) return;
    DrawCmd& c = dl.cmds[dl.count++];
    c.type = DRAW_LINE;
    c.x = x1; c.y = y1;
    c.ink = ink;
    c.line.x2 = x2; c.line.y2 = y2;
    c.line.thickness = thickness;
}

static void addRect(DrawList& dl, int x, int y, int w, int h,
                    Ink ink = INK_BLACK) {
    if (dl.count >= MAX_DRAW_CMDS) return;
    DrawCmd& c = dl.cmds[dl.count++];
    c.type = DRAW_RECT_FILL;
    c.x = x; c.y = y;
    c.ink = ink;
    c.rect.w = w; c.rect.h = h;
}

static void buildNormalStructure(DrawList& dl) {
    // Band separators
    addLine(dl, 0, LAYOUT_HMID_Y, LAYOUT_DISPLAY_W, LAYOUT_HMID_Y, LAYOUT_DIVIDER_H);
    addLine(dl, 0, LAYOUT_HBOT_Y, LAYOUT_DISPLAY_W, LAYOUT_HBOT_Y, LAYOUT_DIVIDER_H);
    // Vertical split: clock | NOW in the top band, day | day in the middle.
    addLine(dl, LAYOUT_VSPLIT_X, 30, LAYOUT_VSPLIT_X, LAYOUT_HMID_Y, LAYOUT_DIVIDER_H);
    addLine(dl, LAYOUT_VSPLIT_X, LAYOUT_HMID_Y, LAYOUT_VSPLIT_X, LAYOUT_HBOT_Y, LAYOUT_DIVIDER_H);
}

static void buildTimerStructure(DrawList& dl) {
    addLine(dl, 0, LAYOUT_HMID_Y, LAYOUT_DISPLAY_W, LAYOUT_HMID_Y, LAYOUT_DIVIDER_H);
    addLine(dl, LAYOUT_VSPLIT_X, 30, LAYOUT_VSPLIT_X, LAYOUT_HMID_Y, LAYOUT_DIVIDER_H);
}

static void buildClockArea(DrawList& dl, const DisplayState& s) {
    addText(dl, LAYOUT_DAY_LABEL_X, LAYOUT_DAY_LABEL_Y, "DAY", FONT_UBUNTU_LABEL, INK_DARK);
    formatDow(buf_dow, sizeof(buf_dow), s.wday);
    addText(dl, LAYOUT_DOW_X, LAYOUT_DOW_Y, buf_dow, FONT_DSEG14_TEXT);

    addText(dl, LAYOUT_DATE_LABEL_X, LAYOUT_DATE_LABEL_Y, "DATE", FONT_UBUNTU_LABEL, INK_DARK);
    formatDate(buf_date, sizeof(buf_date), s.day, s.month);
    addText(dl, LAYOUT_DATE_X, LAYOUT_DATE_Y, buf_date, FONT_DSEG7_DATA);

    // The clock region is partial-updated every minute by renderClockUpdate(),
    // which redraws these same digits at the same origin.
    formatTime(buf_time, sizeof(buf_time), s.hour, s.minute);
    addText(dl, LAYOUT_CLOCK_X, LAYOUT_CLOCK_Y, buf_time, FONT_DSEG7_CLOCK);

    if (s.alarm_valid) {
        addText(dl, LAYOUT_ALARM_LABEL_X, LAYOUT_ALARM_LABEL_Y, "ALARM",
                FONT_UBUNTU_LABEL, INK_DARK);
        formatTime(buf_alarm, sizeof(buf_alarm), s.alarm_hour, s.alarm_minute);
        addText(dl, LAYOUT_ALARM_X, LAYOUT_ALARM_Y, buf_alarm, FONT_DSEG7_DATA);
    }
}

// Integer sine table, 0..90 degrees in 5-degree steps, scaled by 1024.
// Avoids linking libm for a handful of arrows; the panel cannot resolve better
// than a pixel anyway.
static const int16_t SIN_TABLE[19] = {
        0,    89,   178,   265,   350,   433,   512,   587,   658,   724,
      784,   839,   887,   928,   962,   989,  1008,  1020,  1024
};

// sin(deg) * 1024, any integer degree.
static int isin(int deg) {
    deg = ((deg % 360) + 360) % 360;
    if (deg > 180) return -isin(deg - 180);
    if (deg > 90)  deg = 180 - deg;
    int i = deg / 5, r = deg % 5;
    int a = SIN_TABLE[i], b = SIN_TABLE[i + 1 > 18 ? 18 : i + 1];
    return a + (b - a) * r / 5;
}
static int icos(int deg) { return isin(deg + 90); }

// Arrow showing where the wind is going, centred on (cx, cy).
//
// `from_deg` is the meteorological direction the wind blows FROM, so the shaft
// points 180 degrees away from it. Three lines: the shaft and two barbs at the
// head, swept back 30 degrees either side.
static void addWindArrow(DrawList& dl, int cx, int cy, int from_deg, Ink ink) {
    int to = from_deg + 180;
    // Screen y grows downward, so the y components are negated.
    int hx = cx + LAYOUT_ARROW_LEN * isin(to) / 1024;
    int hy = cy - LAYOUT_ARROW_LEN * icos(to) / 1024;
    int tx = cx - LAYOUT_ARROW_LEN * isin(to) / 1024;
    int ty = cy + LAYOUT_ARROW_LEN * icos(to) / 1024;
    addLine(dl, tx, ty, hx, hy, LAYOUT_ARROW_THICK, ink);

    for (int s = -1; s <= 1; s += 2) {
        int b = to + 180 + s * 30;
        addLine(dl, hx, hy,
                hx + LAYOUT_ARROW_BARB * isin(b) / 1024,
                hy - LAYOUT_ARROW_BARB * icos(b) / 1024,
                LAYOUT_ARROW_THICK, ink);
    }
}

// ── Graph primitive ──
//
// One emitter, two time bases. Wind bars grow up from the axis, gust ticks sit
// at gust height, rain hangs below. Bar ink is per column, not per band: hours
// already elapsed are light, the current hour onward is black. `now_index` is
// the first column that counts as "now"; pass 0 to draw everything black, or
// count to draw everything light.
static void buildGraph(DrawList& dl, const HourSlot* slots, int count,
                       int x0, int pitch, int bar_w, int tick_w, int tick_dx,
                       int axis_y, int now_index) {
    for (int i = 0; i < count; i++) {
        const HourSlot& s = slots[i];
        if (!s.valid) continue;
        int x = x0 + i * pitch;
        Ink ink = (i >= now_index) ? INK_BLACK : INK_LIGHT;

        int wh = (int)(s.wind * LAYOUT_PX_PER_MS + 0.5f);
        if (wh > LAYOUT_BAR_MAX_H) wh = LAYOUT_BAR_MAX_H;
        if (wh > 0) addRect(dl, x, axis_y - wh, bar_w, wh, ink);

        // Gust tick is always light — it reads as an annotation on the bar
        // rather than a second series.
        int gh = (int)(s.gust * LAYOUT_PX_PER_MS + 0.5f);
        if (gh > LAYOUT_BAR_MAX_H) gh = LAYOUT_BAR_MAX_H;
        if (gh > 0) {
            addRect(dl, x + tick_dx, axis_y - gh - LAYOUT_TICK_H,
                    tick_w, LAYOUT_TICK_H, INK_LIGHT);
        }

        if (!std::isnan(s.rain) && s.rain > 0.0f) {
            int rh = (int)(s.rain * LAYOUT_PX_PER_MMH + 0.5f);
            if (rh < 1) rh = 1;
            if (rh > LAYOUT_RAIN_MAX_H) rh = LAYOUT_RAIN_MAX_H;
            addRect(dl, x, axis_y + LAYOUT_DIVIDER_H, bar_w, rh, INK_LIGHT);
        }
    }
}

// ── Top band: NOW observations + history strip ──

static char buf_now_wind[8], buf_now_gust[8], buf_now_air[8];
static char buf_hist_dir[HISTORY_SLOTS][6];

static void buildNowBlock(DrawList& dl, const DisplayState& s) {
    addText(dl, LAYOUT_NOW_LABEL_X, LAYOUT_NOW_LABEL_Y, "NOW", FONT_UBUNTU_LABEL, INK_DARK);
    if (s.station_name) {
        addText(dl, LAYOUT_STATION_X, LAYOUT_STATION_Y, s.station_name, FONT_DSEG14_TEXT);
    }

    addText(dl, LAYOUT_NOW_WIND_LX, LAYOUT_NOW_ROW_Y, "WIND", FONT_UBUNTU_LABEL, INK_DARK);
    formatOrDash1(buf_now_wind, sizeof(buf_now_wind), s.wind_speed);
    addText(dl, LAYOUT_NOW_WIND_VX, LAYOUT_NOW_ROW_Y, buf_now_wind, FONT_DSEG7_DATA);

    addText(dl, LAYOUT_NOW_GUST_LX, LAYOUT_NOW_ROW_Y, "G", FONT_UBUNTU_LABEL, INK_DARK);
    formatOrDash1(buf_now_gust, sizeof(buf_now_gust), s.wind_gust);
    addText(dl, LAYOUT_NOW_GUST_VX, LAYOUT_NOW_ROW_Y, buf_now_gust, FONT_DSEG7_DATA);

    addText(dl, LAYOUT_NOW_AIR_LX, LAYOUT_NOW_ROW_Y, "AIR", FONT_UBUNTU_LABEL, INK_DARK);
    formatOrDashInt(buf_now_air, sizeof(buf_now_air), s.air_temp);
    addText(dl, LAYOUT_NOW_AIR_VX, LAYOUT_NOW_ROW_Y, buf_now_air, FONT_DSEG7_DATA);
}

static void buildHistory(DrawList& dl, const WindHistory& h) {
    if (h.count <= 0) return;

    addLine(dl, LAYOUT_HIST_X0 - 3, LAYOUT_HIST_AXIS_Y,
            LAYOUT_HIST_X0 - 3 + HISTORY_SLOTS * LAYOUT_HIST_PITCH,
            LAYOUT_HIST_AXIS_Y, LAYOUT_DIVIDER_H);

    // Every sample is in the past, so the whole strip is black.
    buildGraph(dl, h.slots, h.count, LAYOUT_HIST_X0, LAYOUT_HIST_PITCH,
               LAYOUT_HIST_BAR_W, LAYOUT_HIST_TICK_W, LAYOUT_HIST_TICK_DX,
               LAYOUT_HIST_AXIS_Y, 0);

    // -2h / -1h / NOW sit under columns 0, 4 and 8 — the 15-minute steps make
    // those exactly two hours, one hour and now.
    addText(dl, LAYOUT_HIST_LABEL_X0, LAYOUT_HIST_LABEL_Y, "-2h",
            FONT_UBUNTU_LABEL, INK_DARK);
    addText(dl, LAYOUT_HIST_LABEL_X0 + 4 * LAYOUT_HIST_PITCH, LAYOUT_HIST_LABEL_Y,
            "-1h", FONT_UBUNTU_LABEL, INK_DARK);
    addText(dl, LAYOUT_HIST_LABEL_X0 + 8 * LAYOUT_HIST_PITCH, LAYOUT_HIST_LABEL_Y,
            "NOW", FONT_UBUNTU_LABEL, INK_DARK);

    for (int i = 0; i < h.count && i < HISTORY_SLOTS; i++) {
        if (!h.slots[i].valid) continue;
        addWindArrow(dl,
                     LAYOUT_HIST_DIR_X0 + i * LAYOUT_HIST_PITCH + LAYOUT_HIST_BAR_W / 2,
                     LAYOUT_HIST_DIR_Y, h.slots[i].dir, INK_DARK);
    }
}

// ── Middle band: one forecast day ──

static char buf_fc_dow[FORECAST_DAYS][4];
static char buf_fc_wind[FORECAST_DAYS][12];
static char buf_fc_gust[FORECAST_DAYS][8];
static char buf_fc_air[FORECAST_DAYS][12];
static char buf_fc_sea[FORECAST_DAYS][12];
static char buf_fc_sea2[FORECAST_DAYS][12];
static char buf_fc_hour[FORECAST_DAYS][FORECAST_HOURS][4];
static char buf_fc_dir[FORECAST_DAYS][FORECAST_HOURS][6];
static char buf_chg_time[FORECAST_DAYS][CHANGES_MAX][8];
static char buf_chg_wind[FORECAST_DAYS][CHANGES_MAX][8];
static char buf_chg_gust[FORECAST_DAYS][CHANGES_MAX][8];

static void buildForecastDay(DrawList& dl, const DayForecast& day, int slot,
                             int dx, int now_hour) {
    addText(dl, LAYOUT_FC_DAY_LABEL_X + dx, LAYOUT_FC_DAY_LABEL_Y, "DAY",
            FONT_UBUNTU_LABEL, INK_DARK);
    formatDow(buf_fc_dow[slot], sizeof(buf_fc_dow[slot]), day.wday);
    addText(dl, LAYOUT_FC_DOW_X + dx, LAYOUT_FC_DOW_Y, buf_fc_dow[slot],
            FONT_DSEG14_TEXT);

    // Summary grid: WIND / G on line 1, AIR / SEA on line 2.
    if (day.summary_valid) {
        snprintf(buf_fc_wind[slot], sizeof(buf_fc_wind[slot]), "%.1f-%.1f",
                 (double)day.wind_min, (double)day.wind_max);
        addText(dl, LAYOUT_FC_SUM_LBL1_X + dx, LAYOUT_FC_SUM_L1_Y, "WIND",
                FONT_UBUNTU_LABEL, INK_DARK);
        addText(dl, LAYOUT_FC_SUM_VAL1_X + dx, LAYOUT_FC_SUM_L1_Y,
                buf_fc_wind[slot], FONT_DSEG7_DATA);

        formatOrDash1(buf_fc_gust[slot], sizeof(buf_fc_gust[slot]), day.gust_max);
        addText(dl, LAYOUT_FC_SUM_LBL2_X + dx, LAYOUT_FC_SUM_L1_Y, "G",
                FONT_UBUNTU_LABEL, INK_DARK);
        addText(dl, LAYOUT_FC_SUM_VAL2_X + dx, LAYOUT_FC_SUM_L1_Y,
                buf_fc_gust[slot], FONT_DSEG7_DATA);

        if (!std::isnan(day.air_min) && (day.air_min != 0.0f || day.air_max != 0.0f)) {
            snprintf(buf_fc_air[slot], sizeof(buf_fc_air[slot]), "%d-%d",
                     (int)(day.air_min + 0.5f), (int)(day.air_max + 0.5f));
            addText(dl, LAYOUT_FC_SUM_LBL1_X + dx, LAYOUT_FC_SUM_L2_Y, "AIR",
                    FONT_UBUNTU_LABEL, INK_DARK);
            addText(dl, LAYOUT_FC_SUM_VAL1_X + dx, LAYOUT_FC_SUM_L2_Y,
                    buf_fc_air[slot], FONT_DSEG7_DATA);
        }

        // Clamped to two digits: a three-digit surge plus sign would overrun
        // the column, and the DSEG '-' is a full digit width.
        int smin = day.sea_min, smax = day.sea_max;
        if (smin < -99) smin = -99;
        if (smax > 99) smax = 99;
        formatSea(buf_fc_sea[slot], sizeof(buf_fc_sea[slot]), smin);
        formatSea(buf_fc_sea2[slot], sizeof(buf_fc_sea2[slot]), smax);
        addText(dl, LAYOUT_FC_SUM_LBL2_X + dx, LAYOUT_FC_SUM_L2_Y, "SEA",
                FONT_UBUNTU_LABEL, INK_DARK);
        addText(dl, LAYOUT_FC_SUM_VAL2_X + dx, LAYOUT_FC_SUM_L2_Y,
                buf_fc_sea[slot], FONT_DSEG7_DATA);
        // Second value offset by the width of the first plus a gap. DSEG7 is
        // monospaced: every glyph it renders here — digits and '-' — advances
        // 34px.
        int lo_w = 0;
        for (const char* q = buf_fc_sea[slot]; *q; q++) lo_w += 34;
        addText(dl, LAYOUT_FC_SUM_VAL2_X + dx + lo_w + LAYOUT_FC_SEA_GAP,
                LAYOUT_FC_SUM_L2_Y, buf_fc_sea2[slot], FONT_DSEG7_DATA);
    }

    if (day.valid_count <= 0) return;

    addLine(dl, LAYOUT_FC_X0 + dx - 6, LAYOUT_FC_AXIS_Y,
            LAYOUT_FC_X0 + dx - 6 + LAYOUT_FC_COLS * LAYOUT_FC_PITCH,
            LAYOUT_FC_AXIS_Y, LAYOUT_DIVIDER_H);

    // Bucket the 24 hourly slots into the strip's 12 two-hour columns: wind is
    // averaged over the pair, gust takes the worse of the two (a gust is a peak,
    // averaging it away would understate the day) and rain is averaged too.
    //
    // Rain is a RATE. Precipitation1h is mm/h, so summing two hours gives mm/2h
    // and overstates the intensity the 9px-per-mm/h scale is drawing: a 3.0 +
    // 5.4 pair rendered as 8.4 mm/h, heavy rain, when the worst hour was 5.4.
    HourSlot cols[LAYOUT_FC_COLS] = {};
    for (int i = 0; i < LAYOUT_FC_COLS; i++) {
        int a = i * LAYOUT_FC_HOURS_PER_COL;
        int b = a + 1;
        const HourSlot& sa = day.hours[a];
        const HourSlot& sb = day.hours[b];
        HourSlot& c = cols[i];
        c.hour = a;
        if (sa.valid && sb.valid) {
            c.wind = (sa.wind + sb.wind) * 0.5f;
            c.gust = (sa.gust > sb.gust) ? sa.gust : sb.gust;
            {
                float ra = std::isnan(sa.rain) ? 0.0f : sa.rain;
                float rb = std::isnan(sb.rain) ? 0.0f : sb.rain;
                c.rain = (ra + rb) * 0.5f;
            }
            c.dir  = sa.dir;
            c.sea  = sa.sea;
            c.valid = true;
        } else if (sa.valid) {
            c = sa;
        }
    }

    // now_hour < 0 means the whole day is in the future (tomorrow, or later).
    int now_index = (now_hour < 0) ? 0 : (now_hour / LAYOUT_FC_HOURS_PER_COL);
    buildGraph(dl, cols, LAYOUT_FC_COLS, LAYOUT_FC_X0 + dx, LAYOUT_FC_PITCH,
               LAYOUT_FC_BAR_W, LAYOUT_FC_TICK_W, LAYOUT_FC_TICK_DX,
               LAYOUT_FC_AXIS_Y, now_index);

    for (int i = 0; i < LAYOUT_FC_COLS; i++) {
        if (!cols[i].valid) continue;
        snprintf(buf_fc_hour[slot][i], sizeof(buf_fc_hour[slot][i]), "%02d", cols[i].hour);
        addText(dl, LAYOUT_FC_HOUR_X0 + dx + i * LAYOUT_FC_PITCH, LAYOUT_FC_HOUR_Y,
                buf_fc_hour[slot][i], FONT_UBUNTU_LABEL, INK_DARK);
        addWindArrow(dl,
                     LAYOUT_FC_DIR_X0 + dx + i * LAYOUT_FC_PITCH + LAYOUT_FC_BAR_W / 2,
                     LAYOUT_FC_DIR_Y, cols[i].dir, INK_DARK);
    }

    // CHANGES
    ChangesList ch = {};
    deriveChanges(&ch, &day, 3.0f);
    if (ch.count > 0) {
        addText(dl, LAYOUT_CHG_LABEL_X + dx, LAYOUT_CHG_LABEL_Y, "CHANGES",
                FONT_UBUNTU_LABEL, INK_DARK);
    }
    for (int i = 0; i < ch.count; i++) {
        int y = LAYOUT_CHG_ROW0_Y + i * LAYOUT_CHG_ROW_DY;
        const ChangeEntry& e = ch.entries[i];
        snprintf(buf_chg_time[slot][i], sizeof(buf_chg_time[slot][i]), "%02d:00", e.hour);
        addText(dl, LAYOUT_CHG_TIME_X + dx, y, buf_chg_time[slot][i], FONT_DSEG7_DATA);
        addText(dl, LAYOUT_CHG_VERB_X + dx, y,
                e.kind == CHANGE_BUILDING ? "BUILDING" : "EASING",
                FONT_UBUNTU_LABEL, INK_DARK);
        formatOrDash1(buf_chg_wind[slot][i], sizeof(buf_chg_wind[slot][i]), e.wind);
        addText(dl, LAYOUT_CHG_WIND_X + dx, y, buf_chg_wind[slot][i], FONT_DSEG7_DATA);
        addText(dl, LAYOUT_CHG_G_X + dx, y, "G", FONT_UBUNTU_LABEL, INK_DARK);
        formatOrDash1(buf_chg_gust[slot][i], sizeof(buf_chg_gust[slot][i]), e.gust);
        addText(dl, LAYOUT_CHG_GUST_X + dx, y, buf_chg_gust[slot][i], FONT_DSEG7_DATA);
    }
}

// ── Bottom band: boat bank ──

struct BankCell { const char* label; char* buf; const char* unit; };

static void buildBoatBank(DrawList& dl, const DisplayState& s) {
    formatOrDashInt(buf_solar,   sizeof(buf_solar),   s.solar_w);
    formatOrDashInt(buf_ac,      sizeof(buf_ac),      s.ac_w);
    formatOrDashInt(buf_house,   sizeof(buf_house),   s.house_w);
    formatOrDashInt(buf_battery, sizeof(buf_battery), (float)s.battery_pct);
    formatOrDashInt(buf_device,  sizeof(buf_device),  (float)s.device_pct);
    formatTemp(buf_engine,   sizeof(buf_engine),   s.engine_v);
    formatTemp(buf_saloon_t, sizeof(buf_saloon_t), s.saloon_temp);
    formatOrDashInt(buf_saloon_h, sizeof(buf_saloon_h), s.saloon_humidity);
    formatTemp(buf_icebox,   sizeof(buf_icebox),   s.icebox_temp);

    static char buf_batt_w[8];
    formatOrDashInt(buf_batt_w, sizeof(buf_batt_w), s.house_w);

    const BankCell cells[10] = {
        {"SOLAR",  buf_solar,    "W"},
        {"AC IN",  buf_ac,       "W"},
        {"HOUSE",  buf_house,    "W"},
        {"BATT",   buf_batt_w,   "W"},
        {"SOC",    buf_battery,  "%"},
        {"ENGINE", buf_engine,   "V"},
        {"SALOON", buf_saloon_t, "C"},
        {"HUMID",  buf_saloon_h, "%"},
        {"ICEBOX", buf_icebox,   "C"},
        {"DEVICE", buf_device,   "%"},
    };
    for (int i = 0; i < 10; i++) {
        int x = LAYOUT_BANK_X0 + i * LAYOUT_BANK_PITCH;
        addText(dl, x, LAYOUT_BANK_LABEL_Y, cells[i].label,
                FONT_UBUNTU_LABEL, INK_DARK);
        addText(dl, x + LAYOUT_BANK_VALUE_DX, LAYOUT_BANK_VALUE_Y,
                cells[i].buf, FONT_DSEG7_DATA);
        addText(dl, x + LAYOUT_BANK_UNIT_DX, LAYOUT_BANK_VALUE_Y,
                cells[i].unit, FONT_UBUNTU_LABEL, INK_DARK);
    }
}

static void buildTimer(DrawList& dl, const DisplayState& s) {
    // Timer digits, left-aligned with the clock above them.
    formatTimer(buf_timer, sizeof(buf_timer), s.timer_seconds);
    addText(dl, LAYOUT_TIMER_X, LAYOUT_TIMER_Y, buf_timer, FONT_DSEG7_CLOCK);

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

// Forecast rendering is rebuilt for the Weather Options screen (two hourly
// graph strips) in the layout transcription pass. The old 24-column digit grid
// was removed with ForecastGrid.

// Status bar hints at bottom of screen
static char buf_usb[20];

static void buildStatusBar(DrawList& dl, const DisplayState& s) {
    // Three labels aligned with the three touch zones
    addText(dl, LAYOUT_STATUS_LEFT_X, LAYOUT_STATUS_Y,
            "[ OFF ]", FONT_UBUNTU_LABEL);
    addText(dl, LAYOUT_STATUS_MID_X, LAYOUT_STATUS_Y,
            "[ SETUP ]", FONT_UBUNTU_LABEL);
    snprintf(buf_usb, sizeof(buf_usb), "USB Power: %s",
             s.otg_enabled ? "Out" : "In");
    addText(dl, LAYOUT_STATUS_RIGHT_X, LAYOUT_STATUS_Y,
            buf_usb, FONT_UBUNTU_LABEL);
}

DrawList buildLayout(const DisplayState& state) {
    DrawList dl;
    buildLayoutInto(dl, state);
    return dl;
}

void buildLayoutInto(DrawList& dl, const DisplayState& state) {
    dl = DrawList{};

    // Clock area — always rendered
    buildClockArea(dl, state);

    if (state.timer_active) {
        buildTimerStructure(dl);
        buildTimer(dl, state);
        // Coffee cup bitmap rendered separately by renderTimerFrame()
        Log_info("Layout: timer mode, %ds remaining, %d draw cmds",
                 state.timer_seconds, dl.count);
    } else {
        buildNormalStructure(dl);
        buildNowBlock(dl, state);
        if (state.history) buildHistory(dl, *state.history);

        if (state.days) {
            for (int d = 0; d < FORECAST_DAYS; d++) {
                // Only an un-rolled first day can be partly in the past. Once
                // the flag-down cutoff has moved the window to tomorrow, every
                // column of every strip is ahead of now, so nothing is greyed.
                int now_hour = (d == 0 && !state.forecast_rolled)
                             ? state.hour : -1;
                buildForecastDay(dl, state.days[d], d, d * LAYOUT_DAY_DX, now_hour);
            }
        }

        buildBoatBank(dl, state);
        buildStatusBar(dl, state);
        Log_info("Layout: normal mode, %d draw cmds", dl.count);
    }

    if (dl.count >= MAX_DRAW_CMDS) {
        Log_error("Layout: draw cmd limit reached (%d/%d)!", dl.count, MAX_DRAW_CMDS);
    }
}
