#pragma once

#include <cstdint>
#include <cstddef>

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

    // Electricals (Victron)
    float solar_w;
    float charger_w;
    float battery_w;
    int soc_pct;
    float engine_v;
    int device_pct;

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
};

// ── Layout constants (pixel coordinates from prototype) ──

// Structural
#define LAYOUT_VSPLIT_X      1275
#define LAYOUT_HMID_Y         680
#define LAYOUT_HFORECAST_Y   1024
#define LAYOUT_FC_DIV1_X      677
#define LAYOUT_DISPLAY_W     1872
#define LAYOUT_DISPLAY_H     1404

// Clock area
#define LAYOUT_DOW_X           80
#define LAYOUT_DOW_Y           50
#define LAYOUT_CLOCK_X         40
#define LAYOUT_CLOCK_Y        170
#define LAYOUT_DATE_X         764
#define LAYOUT_DATE_Y          40
#define LAYOUT_ALARM_X        764
#define LAYOUT_ALARM_Y        565

// Data column positions (centred in their zones)
#define LAYOUT_LEFT_DATA_X    764
#define LAYOUT_RIGHT_DATA_X  1355
#define LAYOUT_LABEL_W        120
#define LAYOUT_VALUE_W        270

// Electricals (right column, y positions)
#define LAYOUT_ELEC_Y0         40
#define LAYOUT_ELEC_DY        105

// FMI (left below mid)
#define LAYOUT_FMI_STATION_X   80
#define LAYOUT_FMI_STATION_Y  750
#define LAYOUT_FMI_Y0         700
#define LAYOUT_FMI_DY         105

// Ruuvi (right below mid)
#define LAYOUT_RUUVI_Y0       700
#define LAYOUT_RUUVI_DY       105

// Timer
#define LAYOUT_TIMER_X        936   // centred horizontally
#define LAYOUT_TIMER_Y        860
#define LAYOUT_TIMER_BAR_X     80
#define LAYOUT_TIMER_BAR_Y   1280
#define LAYOUT_TIMER_BAR_W   1712
#define LAYOUT_TIMER_BAR_H     24

// Max draw commands per frame
#define MAX_DRAW_CMDS 200

struct DrawList {
    DrawCmd cmds[MAX_DRAW_CMDS];
    int count;
};

// Build the draw command list for the current state.
// Returns the number of commands.
DrawList buildLayout(const DisplayState& state);
