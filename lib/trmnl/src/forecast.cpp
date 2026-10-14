#include "forecast.h"
#include <cmath>
#include <cstddef>

// m/s as tenths, rounded half away from zero. Readings carry one decimal, so
// this is lossless and lets the CHANGES ranking stay in integers.
static int tenths(float v) {
    return (int)(v * 10.0f + (v < 0 ? -0.5f : 0.5f));
}

// Index of the array element holding local `hour` on day `day_index`, given
// that element 0 is at `start_hour` on day 0. Returns -1 when that hour is
// outside the array.
static int indexFor(int hour, int start_hour, int day_index, int num_hours) {
    int offset = day_index * 24 + hour - start_hour;
    if (offset < 0 || offset >= num_hours) return -1;
    return offset;
}

void fillDayForecast(DayForecast* day,
                     const float* wind, const float* gust,
                     const float* rain, const int* dir,
                     int num_hours, int start_hour, int day_index) {
    if (!day) return;
    day->valid_count = 0;
    for (int h = 0; h < FORECAST_HOURS; h++) {
        HourSlot& s = day->hours[h];
        s = HourSlot{};
        s.hour = h;
        int i = indexFor(h, start_hour, day_index, num_hours);
        if (i < 0) continue;
        if (wind) s.wind = wind[i];
        if (gust) s.gust = gust[i];
        if (rain) s.rain = rain[i];
        if (dir)  s.dir  = dir[i];
        // A NaN reading is a hole in the series, not a zero — leave it invalid
        // so the bar is skipped rather than drawn flat.
        if (std::isnan(s.wind)) continue;
        s.valid = true;
        day->valid_count++;
    }
}

void fillDaySeaLevel(DayForecast* day, const int* sea, int num_hours,
                     int start_hour, int day_index) {
    if (!day || !sea) return;
    for (int h = 0; h < FORECAST_HOURS; h++) {
        int i = indexFor(h, start_hour, day_index, num_hours);
        if (i < 0) continue;
        day->hours[h].sea = sea[i];
    }
}

void computeDaySummary(DayForecast* day, const float* air, int num_hours,
                       int start_hour, int day_index) {
    if (!day) return;
    day->summary_valid = false;
    day->wind_min = day->wind_max = 0.0f;
    day->gust_max = 0.0f;
    day->air_min = day->air_max = 0.0f;
    day->sea_min = day->sea_max = 0;

    bool first = true;
    for (int h = 0; h < FORECAST_HOURS; h++) {
        const HourSlot& s = day->hours[h];
        if (!s.valid) continue;
        if (first) {
            day->wind_min = day->wind_max = s.wind;
            day->gust_max = s.gust;
            day->sea_min = day->sea_max = s.sea;
            first = false;
        } else {
            if (s.wind < day->wind_min) day->wind_min = s.wind;
            if (s.wind > day->wind_max) day->wind_max = s.wind;
            if (s.gust > day->gust_max) day->gust_max = s.gust;
            if (s.sea  < day->sea_min)  day->sea_min  = s.sea;
            if (s.sea  > day->sea_max)  day->sea_max  = s.sea;
        }
    }
    if (first) return;   // no valid slots

    // Air temperature rides in its own array (it is not part of the wind query),
    // so it is scanned separately over the same window.
    bool air_first = true;
    if (air) {
        for (int h = 0; h < FORECAST_HOURS; h++) {
            int i = indexFor(h, start_hour, day_index, num_hours);
            if (i < 0 || std::isnan(air[i])) continue;
            if (air_first) {
                day->air_min = day->air_max = air[i];
                air_first = false;
            } else {
                if (air[i] < day->air_min) day->air_min = air[i];
                if (air[i] > day->air_max) day->air_max = air[i];
            }
        }
    }
    day->summary_valid = true;
}

void fillWindHistory(WindHistory* hist,
                     const float* wind, const float* gust, const int* dir,
                     int num_hours, int start_hour) {
    if (!hist) return;
    hist->count = 0;
    if (num_hours <= 0) return;

    // The strip ends at NOW, so take the most recent HISTORY_SLOTS readings.
    //
    // Every sample keeps its slot even when the reading is NaN. Stations report
    // at different cadences — one that only reports every 30 minutes leaves half
    // the 15-minute samples empty — and compacting past those would slide the
    // remaining bars left, so the column labelled -1h would not be -1h. An
    // invalid slot draws no bar; it does not shift its neighbours.
    int first = num_hours - HISTORY_SLOTS;
    if (first < 0) first = 0;
    for (int i = first; i < num_hours && hist->count < HISTORY_SLOTS; i++) {
        HourSlot& s = hist->slots[hist->count++];
        s = HourSlot{};
        s.hour = (start_hour + i) % 24;
        if (wind) s.wind = wind[i];
        if (gust) s.gust = gust[i];
        if (dir)  s.dir  = dir[i];
        s.valid = !std::isnan(s.wind);
    }
}

int deriveChanges(ChangesList* out, const DayForecast* day, float threshold) {
    (void)threshold;   // kept for API compatibility; no longer used
    if (!out || !day) return 0;
    out->count = 0;

    // The CHANGES list is the day's biggest hour-to-hour moves.
    //
    // Score each hour by a weighted blend of its wind and gust deltas, keep the
    // largest CHANGES_MAX, then put them back in time order. No thresholds and
    // no trend state: a threshold either invents entries on a flat day or drops
    // them on a lively one, and the earlier turning-point version did both.
    // This always fills the rows when there is data, and each row quotes the
    // hour whose values it shows.
    //
    // Scores are integers in tenths of m/s. Wind readings have one decimal, so
    // tenths lose nothing, and integer comparison keeps the ranking exact.
    struct Cand { int h; int delta; };
    Cand cand[FORECAST_HOURS];
    int n = 0;

    int prev = -1;
    for (int h = 0; h < FORECAST_HOURS; h++) {
        if (!day->hours[h].valid) continue;
        if (prev >= 0) {
            const HourSlot& cur = day->hours[h];
            const HourSlot& pre = day->hours[prev];
            int dw = tenths(cur.wind) - tenths(pre.wind);
            // A missing gust reading must not drag the score toward zero, so
            // fall back to the wind delta rather than treating it as no change.
            int dg = (std::isnan(cur.gust) || std::isnan(pre.gust))
                   ? dw : (tenths(cur.gust) - tenths(pre.gust));
            cand[n].h = h;
            cand[n].delta = (CHANGES_WIND_WEIGHT * dw + CHANGES_GUST_WEIGHT * dg)
                          / CHANGES_WEIGHT_SUM;
            n++;
        }
        prev = h;
    }
    if (n == 0) return 0;

    // Partial selection sort by |delta| — n is at most 24, so this is cheaper
    // than pulling in a general sort.
    int keep = (n < CHANGES_MAX) ? n : CHANGES_MAX;
    for (int i = 0; i < keep; i++) {
        int best = i;
        for (int j = i + 1; j < n; j++) {
            int a = cand[j].delta < 0 ? -cand[j].delta : cand[j].delta;
            int b = cand[best].delta < 0 ? -cand[best].delta : cand[best].delta;
            if (a > b) best = j;
        }
        Cand tmp = cand[i]; cand[i] = cand[best]; cand[best] = tmp;
    }

    // Re-sort the survivors by hour so the list reads chronologically.
    for (int i = 0; i < keep; i++) {
        for (int j = i + 1; j < keep; j++) {
            if (cand[j].h < cand[i].h) {
                Cand tmp = cand[i]; cand[i] = cand[j]; cand[j] = tmp;
            }
        }
    }

    for (int i = 0; i < keep; i++) {
        const HourSlot& s = day->hours[cand[i].h];
        ChangeEntry& e = out->entries[out->count++];
        e.hour = s.hour;
        e.kind = (cand[i].delta >= 0) ? CHANGE_BUILDING : CHANGE_EASING;
        e.wind = s.wind;
        e.gust = s.gust;
    }
    return out->count;
}

const char* compassPoint(int degrees) {
    static const char* POINTS[16] = {
        "N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
        "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"
    };
    // Normalise into [0,360) first — FMI can report 360, and a bad reading
    // should wrap rather than index out of bounds.
    int d = degrees % 360;
    if (d < 0) d += 360;
    // 22.5 deg per point, offset by half a sector so N covers 348.75..11.25.
    // Scaled by 4 to keep the half-sector (11.25 deg) in integer arithmetic:
    // idx = round(d / 22.5) == (d*4 + 45) / 90.
    int idx = ((d * 4 + 45) / 90) % 16;
    return POINTS[idx];
}

int flagDownOffset(int now_hour) {
    return (now_hour >= FLAG_DOWN_HOUR) ? 1 : 0;
}
