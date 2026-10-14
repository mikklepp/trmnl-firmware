#pragma once

#include <cstdint>

// ── Hourly forecast model ──
//
// The Weather Options screen draws one bar per hour, so the data is kept at the
// resolution it arrives in. (The previous model bucketed into 1h/2h/4h columns
// to fit a 24-cell digit grid; uniform hourly bars need no such aggregation.)

#define FORECAST_HOURS 24        // slots per day
#define FORECAST_DAYS   2        // the screen shows two forecast days

// One hour of forecast. `valid` is false past the forecast horizon — the last
// strip is drawn short rather than padded.
struct HourSlot {
    float wind;   // m/s
    float gust;   // m/s
    float rain;   // mm/h
    int   dir;    // degrees 0-359
    int   sea;    // cm vs N2000
    int   hour;   // local hour 0-23
    bool  valid;
};

// One day, plus the summary values shown above its graph.
struct DayForecast {
    HourSlot hours[FORECAST_HOURS];
    int   wday;          // 0=Sun .. 6=Sat
    int   valid_count;   // leading valid slots

    // Summary row: "WIND 4-11  G 16 / AIR 8-17  SEA -8+25"
    float wind_min, wind_max;
    float gust_max;
    float air_min, air_max;
    int   sea_min, sea_max;
    bool  summary_valid;
};

// ── Wind history (top band, -2h -> NOW) ──

#define HISTORY_SLOTS 9

struct WindHistory {
    HourSlot slots[HISTORY_SLOTS];
    int count;
};

// ── Derived "CHANGES" timeline ──
//
// Calls out when the wind builds or eases, rather than making the reader scan
// the bars. Derived from the hourly array; no extra data source.

#define CHANGES_MAX 4

// How much each series counts toward an hour's "how much changed" score.
//
// Base wind leads: it is what the bar heights and the summary range are drawn
// from, so the CHANGES rows stay consistent with the rest of the strip. Gusts
// still get a say — they break ties and promote hours where both series move
// together, which is usually the more consequential weather.
//
// Integer weights summing to CHANGES_WEIGHT_SUM. Deltas are converted to
// tenths of m/s first, so the whole ranking is integer arithmetic: no rounding
// questions when comparing scores, and the knob is unambiguous to retune.
// Signed and summed, so a gust holding up against a falling wind legitimately
// damps the score.
//
// 3:1 — the sum is a power of two, so the divide is a shift. Across synthetic
// days 3:1 and 2:1 pick different hours about a third of the time, so this is a
// judgement call rather than something the data settles; both agree with 7:3 on
// the real forecasts checked against the panel.
#define CHANGES_WIND_WEIGHT 3
#define CHANGES_GUST_WEIGHT 1
#define CHANGES_WEIGHT_SUM  4

enum ChangeKind {
    CHANGE_NONE = 0,
    CHANGE_BUILDING,
    CHANGE_EASING,
};

struct ChangeEntry {
    int        hour;    // local hour the change starts
    ChangeKind kind;
    float      wind;    // wind at that hour, m/s
    float      gust;    // gust at that hour, m/s
};

struct ChangesList {
    ChangeEntry entries[CHANGES_MAX];
    int count;
};

// ── Functions ──

// Fill a day's hourly slots from the parsed FMI arrays.
//
// Arrays are hourly, oldest first. `start_hour` is the local hour of the first
// element; `day_index` selects which 24h window to copy (0 = the day containing
// start_hour, 1 = the next day). Slots before the first array element and past
// its end are left invalid, so a partial day renders short.
void fillDayForecast(DayForecast* day,
                     const float* wind, const float* gust,
                     const float* rain, const int* dir,
                     int num_hours, int start_hour, int day_index);

// Which calendar day each forecast strip should show.
//
// The strips are only useful while they still have hours left in them. Late in
// the evening "today" is down to its last column or two, so past FLAG_DOWN_HOUR
// the window rolls forward and the pair becomes tomorrow + the day after. Named
// for the nautical colours-lowering the design's variants are named after.
#define FLAG_DOWN_HOUR 18

// Returns the day_index offset to apply: 0 before the cutoff, 1 after.
int flagDownOffset(int now_hour);

// Fill sea level for a day already populated by fillDayForecast().
// Kept separate because it comes from a different FMI stored query (OAAS).
void fillDaySeaLevel(DayForecast* day, const int* sea, int num_hours,
                     int start_hour, int day_index);

// Compute the summary min/max values from the valid slots.
// Air temperature is passed separately — it is not part of the wind arrays.
void computeDaySummary(DayForecast* day, const float* air, int num_hours,
                       int start_hour, int day_index);

// Fill the -2h -> NOW history strip from hourly observations.
void fillWindHistory(WindHistory* hist,
                     const float* wind, const float* gust, const int* dir,
                     int num_hours, int start_hour);

// Derive the CHANGES entries for a day. Reports the hours where the wind
// meaningfully turns: a run that rises by >= threshold is BUILDING, one that
// falls by >= threshold is EASING. Returns the number of entries written.
int deriveChanges(ChangesList* out, const DayForecast* day, float threshold);

// Wind direction degrees -> 16-point compass ("N", "SSW", ...).
// Returns a static string; never NULL.
const char* compassPoint(int degrees);
