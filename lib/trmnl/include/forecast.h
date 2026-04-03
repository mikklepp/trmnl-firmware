#pragma once

#include <cstdint>

#define FORECAST_COLS 24

// Column layout:
//   0-3:   observations (1h each, past 4 hours)
//   4-7:   forecast 1h (next 4 hours)
//   8-15:  forecast 2h (next 16 hours)
//   16-23: forecast 4h (next 32 hours)
// Total coverage: 4h back + 52h forward

#define FORECAST_OBS_COLS   4   // observation columns
#define FORECAST_FC1H_COLS  4   // 1h forecast columns
#define FORECAST_FC2H_COLS  8   // 2h forecast columns
#define FORECAST_FC4H_COLS  8   // 4h forecast columns

static const int HOURS_PER_COL[FORECAST_COLS] = {
    1,1,1,1,                    // obs (1h)
    1,1,1,1,                    // forecast 1h
    2,2,2,2,2,2,2,2,            // forecast 2h
    4,4,4,4,4,4,4,4             // forecast 4h
};

struct ForecastColumn {
    float wind;   // average wind speed m/s
    float gust;   // max gust within window m/s
    int   dir;    // wind direction degrees (vector-averaged)
    int   sea;    // sea level cm vs N2000 (averaged)
    int   hour;   // start hour (local time, 0-23)
    bool  is_obs; // true = actual observation, false = forecast
    bool  valid;
};

struct ForecastGrid {
    ForecastColumn cols[FORECAST_COLS];
    int valid_count;  // how many columns have data
};

// Fill observation columns (0-3) from hourly FMI observation data.
// hourly arrays are ordered oldest-first. Up to last 4 values used.
// start_hour: local hour of the first (oldest) value.
void fillObservationColumns(
    ForecastGrid* grid,
    const float* hourly_wind,
    const float* hourly_gust,
    const int*   hourly_dir,
    int num_hours,
    int start_hour
);

// Aggregate hourly wind forecast into columns 4-23.
// hourly_wind, hourly_gust: m/s
// hourly_dir: degrees (0-359)
// num_hours: number of hourly forecast values available
// start_hour: local hour of first forecast value (0-23)
void aggregateWindForecast(
    ForecastGrid* grid,
    const float* hourly_wind,
    const float* hourly_gust,
    const int*   hourly_dir,
    int num_hours,
    int start_hour
);

// Aggregate hourly sea level forecast into forecast columns (4-23).
// hourly_sea: cm relative to N2000
void aggregateSeaLevel(
    ForecastGrid* grid,
    const int* hourly_sea,
    int num_hours
);

// Fill sea level for observation columns (0-3).
void fillObservationSeaLevel(
    ForecastGrid* grid,
    const int* hourly_sea,
    int num_hours
);
