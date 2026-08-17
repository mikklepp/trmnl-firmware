#pragma once

#include <cstdint>
#include <cstddef>
#include "forecast.h"

// ── Fonts (map to bb_epaper Group5 font headers) ──

enum FontId {
    FONT_DSEG7_340,    // Clock/timer digits
    FONT_DSEG7_72,     // Data values
    FONT_DSEG7_22,     // Forecast grid
    FONT_DSEG14_72,    // Day-of-week, station name
    FONT_UBUNTU_22,     // Labels and units
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

    // Forecast grid (NULL = no forecast data)
    const ForecastGrid* forecast;
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
//   FONT_DSEG7_340  310    FONT_DSEG7_72   66    FONT_DSEG7_22   19
//   FONT_DSEG14_72   66    FONT_UBUNTU_22  15
// If a font is regenerated at a different size, these must be recomputed.
//
// Non-text Y values (dividers, bars) are plain coordinates — no ascent applies.

// Structural
#define LAYOUT_VSPLIT_X      1275
#define LAYOUT_HMID_Y         680
#define LAYOUT_HFORECAST_Y   1024
#define LAYOUT_FC_DIV1_X      677
#define LAYOUT_DISPLAY_W     1872
#define LAYOUT_DISPLAY_H     1404

// Clock area
#define LAYOUT_DOW_X           80
#define LAYOUT_DOW_Y      (   50 + 66)   // DSEG14 72
#define LAYOUT_CLOCK_X         40
#define LAYOUT_CLOCK_Y    (  170 + 310)  // DSEG7 340
#define LAYOUT_DATE_X         764
#define LAYOUT_DATE_Y     (   40 + 66)   // DSEG7 72
#define LAYOUT_ALARM_X        764
#define LAYOUT_ALARM_Y    (  565 + 66)   // DSEG7 72 (row baseline)

// Data column positions (centred in their zones)
#define LAYOUT_LEFT_DATA_X    764
#define LAYOUT_RIGHT_DATA_X  1355
#define LAYOUT_LABEL_W        120
#define LAYOUT_VALUE_W        270

// Electricals (right column, y positions)
// Rows share a baseline (prototype uses flex align-items: baseline), so the row
// baseline follows the tallest element — the 72px value.
#define LAYOUT_ELEC_Y0    (   40 + 66)   // DSEG7 72
#define LAYOUT_ELEC_DY        105        // row pitch — spacing, not a baseline

// FMI (left below mid)
#define LAYOUT_FMI_STATION_X   80
#define LAYOUT_FMI_STATION_Y (750 + 66)  // DSEG14 72
#define LAYOUT_FMI_Y0     (  700 + 66)   // DSEG7 72
#define LAYOUT_FMI_DY         105

// Ruuvi (right below mid)
#define LAYOUT_RUUVI_Y0   (  700 + 66)   // DSEG7 72
#define LAYOUT_RUUVI_DY       105

// Forecast grid (0,1024 → 1872,1404)
// Row Y values are the digit-cell tops from the prototype; the row labels sit
// 8px lower there (top: 1076 for HOUR) but share these baselines here, which is
// within a pixel or two of the prototype at this size.
#define LAYOUT_FC_LABEL_X      10
#define LAYOUT_FC_ROW_HR_Y   (1068 + 19)  // DSEG7 22
#define LAYOUT_FC_ROW_WIND_Y (1136 + 19)
#define LAYOUT_FC_ROW_GUST_Y (1204 + 19)
#define LAYOUT_FC_ROW_DIR_Y  (1272 + 19)
#define LAYOUT_FC_ROW_SEA_Y  (1340 + 19)
#define LAYOUT_FC_SEG_Y      (1034 + 15)  // Ubuntu 22

// Timer
// NOTE: the prototype centres the timer digits on this X (translateX(-50%)), but
// the renderer treats X as a left edge — so the digits currently sit half a
// string-width right of where they belong. Horizontal alignment is unhandled
// throughout (see .data-value text-align:right and .fc text-align:center); left
// as-is deliberately, pending the alignment pass.
#define LAYOUT_TIMER_X        936   // centred horizontally (not yet honoured)
#define LAYOUT_TIMER_Y    (  860 + 310)  // DSEG7 340
#define LAYOUT_TIMER_BAR_X     80
#define LAYOUT_TIMER_BAR_Y   1280
#define LAYOUT_TIMER_BAR_W   1712
#define LAYOUT_TIMER_BAR_H     24

// Status bar (bottom of screen, below forecast)
#define LAYOUT_STATUS_Y      (1380 + 15)  // Ubuntu 22 — ink ends at 1394 of 1404
#define LAYOUT_STATUS_LEFT_X   80
#define LAYOUT_STATUS_MID_X   830
#define LAYOUT_STATUS_RIGHT_X 1500

// Max draw commands per frame
#define MAX_DRAW_CMDS 200

struct DrawList {
    DrawCmd cmds[MAX_DRAW_CMDS];
    int count;
};

// Build the draw command list for the current state.
// Returns the number of commands.
DrawList buildLayout(const DisplayState& state);

