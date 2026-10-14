#pragma once

#include <cstdint>
#include <cstddef>
#include "forecast.h"

// ── Fonts (map to bb_epaper Group5 font headers) ──

enum FontId {
    FONT_DSEG7_CLOCK,  // 200px clock/timer digits  (102pt, ascent 199)
    FONT_DSEG7_DATA,   // 40px values               ( 21pt, ascent 40)
    FONT_DSEG14_TEXT,  // 40px day-of-week, station ( 21pt, ascent 40)
    FONT_UBUNTU_LABEL, // 24px labels and units     ( 12pt, ascent 18, cap 17)
    LAYOUT_FONT_COUNT,
};

// ── Ink levels (2bpp) ──
//
// The panel runs in BB_MODE_2BPP, which gives four levels. These are the raw
// pixel values the FastEPD 2bpp path expects — see the (old,new) lookup table in
// bbep2BppPartial(), which names them black / dark gray / light gray / white.
//
// NOTE: do NOT use BBEP_BLACK / BBEP_WHITE with these. Those constants are 0 and
// 1, so BBEP_WHITE means *dark gray* in 2bpp mode — a silent, wrong-looking
// render rather than a compile error.
enum Ink {
    INK_BLACK = 0,
    INK_DARK  = 1,   // dark gray
    INK_LIGHT = 2,   // light gray
    INK_WHITE = 3,
};

// ── Draw commands ──

enum DrawType {
    DRAW_TEXT,
    DRAW_LINE,
    DRAW_RECT_FILL,
};

struct DrawCmd {
    DrawType type;
    int x, y;
    Ink ink;
    union {
        struct { const char* text; FontId font; } text;
        struct { int x2, y2, thickness; } line;
        struct { int w, h; } rect;
    };
};

// ── Display state (all the data needed to render one frame) ──

struct DisplayState {
    // Clock
    int hour, minute;
    int day, month;
    int wday;             // 0=Sun, 3=Wed

    // Alarm
    int alarm_hour, alarm_minute;
    bool alarm_valid;

    // Energy flow
    float solar_w;        // SmartSolar PV input (always >= 0)
    float ac_w;           // VE.Bus battery power (+ = shore in, - = inverting)
    float house_w;        // computed: shunt - solar_batt - vebus
    int battery_pct;      // SoC from SmartShunt
    float engine_v;       // aux/starter voltage from SmartShunt
    int device_pct;       // TRMNL device battery from BQ27427

    // FMI observations
    const char* station_name;
    float wind_speed;     // m/s
    float wind_gust;      // m/s
    int wind_dir;         // degrees
    float air_temp;       // degC — the NOW block's AIR reading

    // Ruuvi
    float saloon_temp;
    float saloon_humidity;
    float icebox_temp;

    // Timer
    bool timer_active;
    int timer_seconds;     // remaining
    int timer_total;       // for progress bar

    // Status
    bool otg_enabled;      // USB OTG (power out) active

    // Forecast: two hourly days (NULL = no forecast data)
    const DayForecast* days;      // array of FORECAST_DAYS
    const WindHistory* history;   // -2h -> NOW strip, NULL if unavailable
    // True once the flag-down cutoff has rolled the window forward, i.e. the
    // first strip is tomorrow rather than today. Every column is then in the
    // future and none should be greyed as elapsed.
    bool forecast_rolled;
};

// ── Layout constants (pixel coordinates from prototype) ──
//
// Y coordinates for text are BASELINES, because bb_epaper's setCursor() takes a
// baseline (bb_ep_gfx.inl draws each glyph at y + glyph->yOffset, and yOffset is
// negative). layout-prototype.html positions text with CSS `top` — the top of the
// line box — so every text Y here is written as `prototype_top + ascent`, where
// ascent is that font's distance from baseline to the top of its tallest glyph.
//
// Ascents, measured from the generated font headers (max -yOffset over the glyphs
// each font actually renders):
//   FONT_DSEG7_CLOCK 199   FONT_DSEG7_DATA 40
//   FONT_DSEG14_TEXT  40   FONT_UBUNTU_LABEL 18 (descent 5)
// If a font is regenerated at a different size, these must be recomputed.
//
// Design -> firmware Y conversion. The design positions text with CSS
// `line-height: 0`, which puts the baseline at
//     baseline = top + (0 - (ascent + descent)) / 2 + ascent
//              = top + (ascent - descent) / 2
// NOT at `top`. For the DSEG faces (descent 0) that is simply top + ascent/2.
// Verified against the design: WED/04-02 (DSEG 40) and DAY (Ubuntu 24) all land
// on baseline ~76, and ALARM/21:04 both on ~400, as their shared rows require.
#define FONT_ASCENT_CLOCK   199
#define FONT_ASCENT_DATA     40
#define FONT_ASCENT_TEXT     40
#define FONT_ASCENT_LABEL    18
#define FONT_DESCENT_LABEL    5
//
// Non-text Y values (dividers, bars) are plain coordinates — no ascent applies.

// All Y values below are BASELINES, converted from the design's CSS `top` by
// the rule above. Non-text Y values (dividers, bars, graph axes) are plain
// coordinates — no ascent applies.

// ── Structure ──
#define LAYOUT_DISPLAY_W      1872
#define LAYOUT_DISPLAY_H      1404
#define LAYOUT_VSPLIT_X        912   // splits both the top and middle bands
#define LAYOUT_HMID_Y          470   // top | middle
// Middle | bottom. The design puts this at 1180; it is raised here to buy the
// forecast strips a second summary line for sea level. The bottom band carried
// ~115px of air (43 above the labels, 27 mid, 45 below the status row) and
// still has 187px against a 172px minimum. See the plan.
#define LAYOUT_HBOT_Y         1217
#define LAYOUT_DIVIDER_H         2

// ── Top band: clock ──
#define LAYOUT_DAY_LABEL_X      80
#define LAYOUT_DAY_LABEL_Y      76
#define LAYOUT_DOW_X           190
#define LAYOUT_DOW_Y            76
#define LAYOUT_DATE_LABEL_X    480
#define LAYOUT_DATE_LABEL_Y     76
#define LAYOUT_DATE_X          600
#define LAYOUT_DATE_Y           76
#define LAYOUT_CLOCK_X          80
#define LAYOUT_CLOCK_ASCENT   FONT_ASCENT_CLOCK
// Width of the region renderClockUpdate() may clear. The clock's own ink ends
// around x=773 ("88:88" at 163px/digit); everything right of the vertical split
// belongs to the NOW block and the history graph, which share these rows and
// must survive the minute tick.
#define LAYOUT_CLOCK_CLEAR_W  (LAYOUT_VSPLIT_X - 8)
#define LAYOUT_CLOCK_Y         316
#define LAYOUT_ALARM_LABEL_X   480
#define LAYOUT_ALARM_LABEL_Y   400
#define LAYOUT_ALARM_X         600
#define LAYOUT_ALARM_Y         400

// ── Top band: NOW observations (right of the split) ──
#define LAYOUT_NOW_LABEL_X     992
#define LAYOUT_NOW_LABEL_Y      76
#define LAYOUT_STATION_X      1102
#define LAYOUT_STATION_Y        76
#define LAYOUT_NOW_ROW_Y       122
#define LAYOUT_NOW_WIND_LX     992
#define LAYOUT_NOW_WIND_VX    1102
#define LAYOUT_NOW_GUST_LX    1312
#define LAYOUT_NOW_GUST_VX    1352
#define LAYOUT_NOW_AIR_LX     1492
#define LAYOUT_NOW_AIR_VX     1572

// ── Top band: -2h -> NOW history graph ──
// 9 samples at 15-minute steps: 8 intervals is exactly 2h.
#define LAYOUT_HIST_AXIS_Y     302
#define LAYOUT_HIST_X0         998   // first bar left edge
#define LAYOUT_HIST_PITCH       88
#define LAYOUT_HIST_BAR_W       76
#define LAYOUT_HIST_TICK_W      82
#define LAYOUT_HIST_TICK_DX      -3  // tick is wider than the bar, centred on it
#define LAYOUT_HIST_LABEL_Y    389   // -2h / -1h / NOW
#define LAYOUT_HIST_LABEL_X0  1015
#define LAYOUT_HIST_DIR_Y      417
#define LAYOUT_HIST_DIR_X0    1016

// ── Middle band: two forecast days ──
// The right day repeats every X at +LAYOUT_DAY_DX.
#define LAYOUT_DAY_DX          912
#define LAYOUT_FC_DAY_LABEL_X   80
#define LAYOUT_FC_DAY_LABEL_Y  558
#define LAYOUT_FC_DOW_X        190
#define LAYOUT_FC_DOW_Y        558

// Summary as a 2x2 grid on two lines (design has one line; the second makes
// room for sea level). Columns are shared by both lines so labels and values
// align vertically.
#define LAYOUT_FC_SUM_L1_Y     604
#define LAYOUT_FC_SUM_L2_Y     650   // +46: one 40px line plus breathing room
#define LAYOUT_FC_SUM_LBL1_X    80
#define LAYOUT_FC_SUM_VAL1_X   190
#define LAYOUT_FC_SUM_LBL2_X   500
#define LAYOUT_FC_SUM_VAL2_X   610
// The sea range is drawn as two separate values rather than one string. DSEG7
// has no space glyph (its range starts at '+'), and its '+' is a narrow 15px
// sign cell against 34px digits — so "-8+25" runs together and the '+' reads as
// a stray mark rather than the start of the second number. Splitting them puts
// a real gap under layout control. 205px worst case ("-99" + gap + "+99") in
// the 302px available before the strip edge.
#define LAYOUT_FC_SEA_GAP       20

// Forecast graph. Everything below the summary shifts down by the 46px the
// second line costs, relative to the design.
// The forecast strip is 12 columns of TWO hours each, not 24 hourly ones: the
// design's hour labels read 00 02 04 ... 22 and there are exactly 12 bars per
// day at 66px pitch, which is what fits the 912px half-width.
#define LAYOUT_FC_COLS          12
#define LAYOUT_FC_HOURS_PER_COL  2
#define LAYOUT_FC_AXIS_Y       830   // design 784 + 46
#define LAYOUT_FC_X0            86
#define LAYOUT_FC_PITCH         66
#define LAYOUT_FC_BAR_W         54
#define LAYOUT_FC_TICK_W        60
#define LAYOUT_FC_TICK_DX       -3
#define LAYOUT_FC_HOUR_Y       917   // design 871 + 46
#define LAYOUT_FC_HOUR_X0       97
#define LAYOUT_FC_DIR_Y        945   // design 899 + 46
#define LAYOUT_FC_DIR_X0        85

// CHANGES block
#define LAYOUT_CHG_LABEL_X      80
#define LAYOUT_CHG_LABEL_Y    1015   // design 969 + 46
#define LAYOUT_CHG_ROW0_Y     1065   // design 1019 + 46
#define LAYOUT_CHG_ROW_DY       44
#define LAYOUT_CHG_TIME_X       80
#define LAYOUT_CHG_VERB_X      244
#define LAYOUT_CHG_WIND_X      429
// G and the gust value sit further right than the design's 520/560 to leave the
// wind column room for a decimal ("10.1" needs 102px from 429).
#define LAYOUT_CHG_G_X         545
#define LAYOUT_CHG_GUST_X      585

// Wind direction arrows. Drawn from the degrees rather than picked from a set
// of pre-rendered headings, so the angle is exact. Meteorological convention:
// the direction is where the wind blows FROM, and the arrow points the way it
// is going — i.e. 180 degrees opposite.
#define LAYOUT_ARROW_LEN        18   // shaft length, px
#define LAYOUT_ARROW_BARB        7   // barb length, px
#define LAYOUT_ARROW_THICK       4

// ── Graph scales (shared by both bands) ──
// Wind bars grow up from the axis, rain hangs below it.
// 10px per m/s, up from the design's 7. A strong Baltic day was drawing barely
// half the available height, which read as unremarkable; at this scale 15 m/s
// fills most of the band and looks like what it is.
#define LAYOUT_PX_PER_MS        10   // px per m/s
#define LAYOUT_PX_PER_MMH        9   // px per mm/h
#define LAYOUT_TICK_H            4
// Wind/gust clamp. Sized to the 170px between the axis and the row above it, so
// the scale increase is not immediately eaten by the ceiling — at 140 a 15 m/s
// day and a 25 m/s day would have drawn the same flat-topped bar.
#define LAYOUT_BAR_MAX_H       168
// Rain hangs below the axis and has far less room than the wind bars above it:
// only as far as the hour labels. 140 never clamped anything here, so a heavy
// hour drew straight through the labels.
#define LAYOUT_RAIN_MAX_H       60

// ── Bottom band: boat bank ──
// Shifted +17 from the design, which balances the band inside its new bounds:
// 28px of air above the labels and 28px below the status row.
#define LAYOUT_BANK_LABEL_Y   1265   // design 1248 + 17
#define LAYOUT_BANK_VALUE_Y   1317   // design 1300 + 17
#define LAYOUT_BANK_X0          80
#define LAYOUT_BANK_PITCH      170
#define LAYOUT_BANK_VALUE_DX     2   // values sit ~2px right of their label
#define LAYOUT_BANK_UNIT_DX    112   // unit follows the value

// Status row
#define LAYOUT_STATUS_Y       1371   // design 1354 + 17
#define LAYOUT_STATUS_LEFT_X    80
#define LAYOUT_STATUS_MID_X    836
#define LAYOUT_STATUS_RIGHT_X 1552

// ── Timer overlay (unchanged behaviour; coordinates follow the new fonts) ──
//
// The timer digits sit directly below the clock and share its left edge, so the
// two read as one column rather than one left-aligned and one centred. DSEG7 is
// monospaced, so widths are a per-glyph sum — no text measurement pass needed.
#define LAYOUT_TIMER_X         LAYOUT_CLOCK_X
#define LAYOUT_TIMER_DIGIT_W   163   // DSEG7 clock face, monospaced
#define LAYOUT_TIMER_COLON_W    40
#define LAYOUT_TIMER_Y         860
#define LAYOUT_TIMER_BAR_X      80
#define LAYOUT_TIMER_BAR_Y    1280
#define LAYOUT_TIMER_BAR_W    1712
#define LAYOUT_TIMER_BAR_H      24

// Coffee cup artwork origin. Its own constant, not LAYOUT_VSPLIT_X: the cup
// used to be anchored to the split, and when that moved 1275 -> 912 for the new
// screen the artwork slid left on top of the timer digits. With the digits
// centred on 936 and ending at x=1201, this leaves a 122px gap and keeps the
// 500px-wide mug inside the right edge.
// The cup is centred in the right half — the area beyond the vertical split.
// It used to sit at a hardcoded 1275, left over from when the split was there.
#define LAYOUT_CUP_ZONE_W     (LAYOUT_DISPLAY_W - LAYOUT_VSPLIT_X)
#define LAYOUT_CUP_X           LAYOUT_VSPLIT_X
#define LAYOUT_CUP_Y             0

// Max draw commands per frame.
//
// The Weather Options screen is far denser than the old one: two forecast days
// at up to 24 hourly bars each, plus their gust ticks and rain bars, is ~250
// commands on its own before the clock, NOW block, history strip, CHANGES rows
// and boat bank. Worst case measured at ~333. Sized with headroom — overflow is
// silent truncation mid-screen, and DrawCmd is 24 bytes so 448 costs ~10 KB.
#define MAX_DRAW_CMDS 448

struct DrawList {
    DrawCmd cmds[MAX_DRAW_CMDS];
    int count;
};

// Build the draw command list for the current state.
//
// DrawList is ~14 KB — 448 commands of 32 bytes. The Arduino loop task has a
// 16 KB stack, so returning one by value leaves ~2 KB for everything else and
// the FMI fetch panics inside HTTPClient. Firmware paths must use the *Into
// form with a static buffer; the by-value wrapper is for the native tests,
// where stack depth is not a constraint. Same rule as parseFmiResponseInto()
// in fmi_parse.h, for the same reason.
void buildLayoutInto(DrawList& out, const DisplayState& state);
DrawList buildLayout(const DisplayState& state);

