#include "forecast.h"
#include "trmnl_log.h"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ── Shared aggregation helper ──

static void aggregateSpan(
    ForecastColumn* col,
    const float* wind, const float* gust, const int* dir,
    int count, int hour, bool is_obs
) {
    if (count <= 0) return;

    float wind_sum = 0, gust_max = 0;
    float dir_sin = 0, dir_cos = 0;

    for (int i = 0; i < count; i++) {
        wind_sum += wind[i];
        if (gust[i] > gust_max) gust_max = gust[i];
        float rad = dir[i] * (float)M_PI / 180.0f;
        dir_sin += sinf(rad);
        dir_cos += cosf(rad);
    }

    col->wind = wind_sum / count;
    col->gust = gust_max;

    float avg_rad = atan2f(dir_sin / count, dir_cos / count);
    int avg_deg = (int)(avg_rad * 180.0f / (float)M_PI + 0.5f);
    if (avg_deg < 0) avg_deg += 360;
    col->dir = avg_deg;

    col->hour = hour % 24;
    col->is_obs = is_obs;
    col->valid = true;
}

// ── Observation columns (0-3): last 4 hours of actual data ──

void fillObservationColumns(
    ForecastGrid* grid,
    const float* hourly_wind,
    const float* hourly_gust,
    const int*   hourly_dir,
    int num_hours,
    int start_hour
) {
    Log_verbose("Forecast obs: %d hours from hour %d", num_hours, start_hour);
    // Take the last FORECAST_OBS_COLS values (most recent observations)
    int offset = num_hours - FORECAST_OBS_COLS;
    if (offset < 0) offset = 0;
    int available = num_hours - offset;

    // Fill from the right — if we have fewer than 4, leave early columns empty
    int col_start = FORECAST_OBS_COLS - available;

    for (int i = 0; i < available; i++) {
        int src = offset + i;
        int col = col_start + i;
        int hour = (start_hour + src) % 24;

        aggregateSpan(
            &grid->cols[col],
            &hourly_wind[src], &hourly_gust[src], &hourly_dir[src],
            1, hour, true
        );
        grid->valid_count++;
    }
}

// ── Forecast columns (4-23): 4×1h + 8×2h + 8×4h ──

void aggregateWindForecast(
    ForecastGrid* grid,
    const float* hourly_wind,
    const float* hourly_gust,
    const int*   hourly_dir,
    int num_hours,
    int start_hour
) {
    Log_info("Forecast: aggregating %d hours from hour %d into cols %d-%d",
             num_hours, start_hour, FORECAST_OBS_COLS, FORECAST_COLS - 1);
    int src = 0;

    for (int col = FORECAST_OBS_COLS; col < FORECAST_COLS && src < num_hours; col++) {
        int span = HOURS_PER_COL[col];
        int actual_span = (src + span <= num_hours) ? span : (num_hours - src);
        int hour = (start_hour + src) % 24;

        aggregateSpan(
            &grid->cols[col],
            &hourly_wind[src], &hourly_gust[src], &hourly_dir[src],
            actual_span, hour, false
        );
        grid->valid_count++;
        src += span;  // advance by full span even if partial
    }
}

// ── Sea level for forecast columns ──

void aggregateSeaLevel(
    ForecastGrid* grid,
    const int* hourly_sea,
    int num_hours
) {
    int src = 0;

    for (int col = FORECAST_OBS_COLS; col < FORECAST_COLS && src < num_hours; col++) {
        int span = HOURS_PER_COL[col];
        int actual_span = (src + span <= num_hours) ? span : (num_hours - src);
        int sum = 0;

        for (int i = 0; i < actual_span; i++) {
            sum += hourly_sea[src + i];
        }
        grid->cols[col].sea = sum / actual_span;
        src += span;
    }
}

// ── Sea level for observation columns ──

void fillObservationSeaLevel(
    ForecastGrid* grid,
    const int* hourly_sea,
    int num_hours
) {
    int offset = num_hours - FORECAST_OBS_COLS;
    if (offset < 0) offset = 0;
    int available = num_hours - offset;
    int col_start = FORECAST_OBS_COLS - available;

    for (int i = 0; i < available; i++) {
        grid->cols[col_start + i].sea = hourly_sea[offset + i];
    }
}
