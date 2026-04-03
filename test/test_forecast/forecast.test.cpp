#include <unity.h>
#include <forecast.h>
#include <cmath>

void setUp(void) {}
void tearDown(void) {}

// ── Observation columns (0-3) ──

void test_obs_fills_last_4(void) {
    // 6 hourly observations — should take the last 4
    float wind[] = {5, 6, 7, 8, 9, 10};
    float gust[] = {8, 9, 10, 11, 12, 13};
    int   dir[]  = {180, 190, 200, 210, 220, 230};

    ForecastGrid grid = {};
    fillObservationColumns(&grid, wind, gust, dir, 6, 10);

    // Columns 0-3 should have hours 12,13,14,15 (start_hour=10, offset=2)
    TEST_ASSERT_TRUE(grid.cols[0].valid);
    TEST_ASSERT_TRUE(grid.cols[0].is_obs);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 7.0f, grid.cols[0].wind);
    TEST_ASSERT_EQUAL_INT(12, grid.cols[0].hour);

    TEST_ASSERT_FLOAT_WITHIN(0.1f, 10.0f, grid.cols[3].wind);
    TEST_ASSERT_EQUAL_INT(15, grid.cols[3].hour);
}

void test_obs_fewer_than_4(void) {
    // Only 2 observations — columns 0-1 empty, 2-3 filled
    float wind[] = {5, 6};
    float gust[] = {8, 9};
    int   dir[]  = {180, 190};

    ForecastGrid grid = {};
    fillObservationColumns(&grid, wind, gust, dir, 2, 14);

    TEST_ASSERT_FALSE(grid.cols[0].valid);
    TEST_ASSERT_FALSE(grid.cols[1].valid);
    TEST_ASSERT_TRUE(grid.cols[2].valid);
    TEST_ASSERT_TRUE(grid.cols[3].valid);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 5.0f, grid.cols[2].wind);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 6.0f, grid.cols[3].wind);
}

// ── Forecast columns (4-23) ──

void test_forecast_1h_columns(void) {
    // 8 hourly forecast values — fills cols 4-7 (1h) + cols 8-9 (2h)
    float wind[] = {8, 7, 9, 11, 6, 5, 7, 8};
    float gust[] = {12, 11, 14, 16, 10, 9, 11, 12};
    int   dir[]  = {225, 230, 240, 250, 235, 220, 210, 200};

    ForecastGrid grid = {};
    aggregateWindForecast(&grid, wind, gust, dir, 8, 13);

    // Col 4: first 1h forecast
    TEST_ASSERT_TRUE(grid.cols[4].valid);
    TEST_ASSERT_FALSE(grid.cols[4].is_obs);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 8.0f, grid.cols[4].wind);
    TEST_ASSERT_EQUAL_INT(13, grid.cols[4].hour);

    // Col 7: last 1h forecast
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 11.0f, grid.cols[7].wind);
    TEST_ASSERT_EQUAL_INT(16, grid.cols[7].hour);

    // Col 8: first 2h forecast — average of hours 4-5 (values[4..5])
    TEST_ASSERT_TRUE(grid.cols[8].valid);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 5.5f, grid.cols[8].wind);  // (6+5)/2
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 10.0f, grid.cols[8].gust); // max(10,9)
}

void test_forecast_gust_takes_max(void) {
    // Need 4 (1h) + 16 (2h) + 4 (first 4h col) = 24 hourly values
    float wind[24], gust[24];
    int dir[24];
    for (int i = 0; i < 24; i++) { wind[i] = 5; gust[i] = 5; dir[i] = 180; }
    // Override the 4h bucket (src indices 20-23)
    wind[20] = 3; wind[21] = 8; wind[22] = 2; wind[23] = 9;
    gust[20] = 4; gust[21] = 15; gust[22] = 3; gust[23] = 20;

    ForecastGrid grid = {};
    aggregateWindForecast(&grid, wind, gust, dir, 24, 0);

    // Col 16: first 4h column
    TEST_ASSERT_TRUE(grid.cols[16].valid);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 5.5f, grid.cols[16].wind);  // (3+8+2+9)/4
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 20.0f, grid.cols[16].gust); // max
}

void test_forecast_direction_wrap(void) {
    // 4 hours for first 1h cols, then 2h col averaging 350° and 10°
    float wind[] = {5, 5, 5, 5, 5, 5};
    float gust[] = {5, 5, 5, 5, 5, 5};
    int   dir[]  = {180, 180, 180, 180, 350, 10};

    ForecastGrid grid = {};
    aggregateWindForecast(&grid, wind, gust, dir, 6, 0);

    // Col 8: 2h avg of 350° and 10° → ~0°
    TEST_ASSERT_TRUE(grid.cols[8].valid);
    TEST_ASSERT_INT_WITHIN(5, 0, grid.cols[8].dir);
}

void test_forecast_hour_wrap(void) {
    float wind[8] = {5,5,5,5,5,5,5,5};
    float gust[8] = {5,5,5,5,5,5,5,5};
    int   dir[8]  = {180,180,180,180,180,180,180,180};

    ForecastGrid grid = {};
    aggregateWindForecast(&grid, wind, gust, dir, 8, 22);

    // Col 4: hour 22, col 5: 23, col 6: 0, col 7: 1
    TEST_ASSERT_EQUAL_INT(22, grid.cols[4].hour);
    TEST_ASSERT_EQUAL_INT(23, grid.cols[5].hour);
    TEST_ASSERT_EQUAL_INT(0, grid.cols[6].hour);
    TEST_ASSERT_EQUAL_INT(1, grid.cols[7].hour);
}

void test_forecast_empty_input(void) {
    ForecastGrid grid = {};
    aggregateWindForecast(&grid, NULL, NULL, NULL, 0, 0);
    TEST_ASSERT_EQUAL_INT(0, grid.valid_count);
}

// ── Sea level ──

void test_sea_level_forecast(void) {
    // 6 hourly values: fills 4 × 1h + 1 × 2h
    int sea[] = {12, 15, 18, 22, 10, 5};

    ForecastGrid grid = {};
    // Pre-fill some forecast cols so we can verify sea is merged
    grid.cols[4].valid = true;
    aggregateSeaLevel(&grid, sea, 6);

    TEST_ASSERT_EQUAL_INT(12, grid.cols[4].sea);
    TEST_ASSERT_EQUAL_INT(15, grid.cols[5].sea);
    TEST_ASSERT_EQUAL_INT(7, grid.cols[8].sea);  // (10+5)/2
}

void test_sea_level_observations(void) {
    int sea[] = {5, 8, 12, 15, 18, 22};

    ForecastGrid grid = {};
    fillObservationSeaLevel(&grid, sea, 6);

    // Last 4: 12, 15, 18, 22
    TEST_ASSERT_EQUAL_INT(12, grid.cols[0].sea);
    TEST_ASSERT_EQUAL_INT(22, grid.cols[3].sea);
}

// ── Combined obs + forecast ──

void test_combined_grid(void) {
    // 6 obs + 8 forecast
    float obs_wind[] = {4, 5, 6, 7, 8, 9};
    float obs_gust[] = {6, 7, 8, 9, 10, 11};
    int   obs_dir[]  = {180, 185, 190, 195, 200, 205};

    float fc_wind[] = {10, 11, 12, 13, 7, 6, 8, 9};
    float fc_gust[] = {14, 15, 16, 17, 10, 9, 12, 13};
    int   fc_dir[]  = {210, 215, 220, 225, 230, 235, 240, 245};

    ForecastGrid grid = {};
    fillObservationColumns(&grid, obs_wind, obs_gust, obs_dir, 6, 8);
    aggregateWindForecast(&grid, fc_wind, fc_gust, fc_dir, 8, 14);

    // Obs cols 0-3: last 4 of obs (values at indices 2-5)
    TEST_ASSERT_TRUE(grid.cols[0].is_obs);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 6.0f, grid.cols[0].wind);
    TEST_ASSERT_EQUAL_INT(10, grid.cols[0].hour);

    // Forecast col 4: first 1h forecast
    TEST_ASSERT_FALSE(grid.cols[4].is_obs);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 10.0f, grid.cols[4].wind);
    TEST_ASSERT_EQUAL_INT(14, grid.cols[4].hour);

    // Forecast col 8: first 2h forecast, avg of fc[4..5]
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 6.5f, grid.cols[8].wind);
}

void test_obs_is_obs_flag(void) {
    float wind[] = {5, 6, 7, 8};
    float gust[] = {8, 9, 10, 11};
    int   dir[]  = {180, 190, 200, 210};

    ForecastGrid grid = {};
    fillObservationColumns(&grid, wind, gust, dir, 4, 10);
    aggregateWindForecast(&grid, wind, gust, dir, 4, 14);

    for (int i = 0; i < 4; i++) {
        TEST_ASSERT_TRUE_MESSAGE(grid.cols[i].is_obs, "Obs col should be is_obs");
    }
    for (int i = 4; i < 8; i++) {
        if (grid.cols[i].valid) {
            TEST_ASSERT_FALSE_MESSAGE(grid.cols[i].is_obs, "Forecast col should not be is_obs");
        }
    }
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_obs_fills_last_4);
    RUN_TEST(test_obs_fewer_than_4);
    RUN_TEST(test_forecast_1h_columns);
    RUN_TEST(test_forecast_gust_takes_max);
    RUN_TEST(test_forecast_direction_wrap);
    RUN_TEST(test_forecast_hour_wrap);
    RUN_TEST(test_forecast_empty_input);
    RUN_TEST(test_sea_level_forecast);
    RUN_TEST(test_sea_level_observations);
    RUN_TEST(test_combined_grid);
    RUN_TEST(test_obs_is_obs_flag);
    UNITY_END();
    return 0;
}
