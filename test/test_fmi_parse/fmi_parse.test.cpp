#include <unity.h>
#include <fmi_parse.h>
#include <cmath>
#include <cstring>

void setUp(void) {}
void tearDown(void) {}

// Minimal observation XML with 3 values per parameter, 6 parameters
// Order: t2m, ws_10min, wg_pt10m_max, wd_10min, rh, p_sea
static const char* OBS_XML =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
    "<wfs:FeatureCollection>"
    "<wml2:MeasurementTimeseries gml:id=\"obs-1-t2m\">"
    "<wml2:value>2.6</wml2:value>"
    "<wml2:value>2.4</wml2:value>"
    "<wml2:value>2.0</wml2:value>"
    "</wml2:MeasurementTimeseries>"
    "<wml2:MeasurementTimeseries gml:id=\"obs-1-ws_10min\">"
    "<wml2:value>6.5</wml2:value>"
    "<wml2:value>6.6</wml2:value>"
    "<wml2:value>4.7</wml2:value>"
    "</wml2:MeasurementTimeseries>"
    "<wml2:MeasurementTimeseries gml:id=\"obs-1-wg_pt10m_max\">"
    "<wml2:value>7.6</wml2:value>"
    "<wml2:value>7.6</wml2:value>"
    "<wml2:value>5.8</wml2:value>"
    "</wml2:MeasurementTimeseries>"
    "<wml2:MeasurementTimeseries gml:id=\"obs-1-wd_10min\">"
    "<wml2:value>210.0</wml2:value>"
    "<wml2:value>215.0</wml2:value>"
    "<wml2:value>225.0</wml2:value>"
    "</wml2:MeasurementTimeseries>"
    "<wml2:MeasurementTimeseries gml:id=\"obs-1-rh\">"
    "<wml2:value>78.0</wml2:value>"
    "<wml2:value>80.0</wml2:value>"
    "<wml2:value>82.0</wml2:value>"
    "</wml2:MeasurementTimeseries>"
    "<wml2:MeasurementTimeseries gml:id=\"obs-1-p_sea\">"
    "<wml2:value>1013.2</wml2:value>"
    "<wml2:value>1013.0</wml2:value>"
    "<wml2:value>1012.8</wml2:value>"
    "</wml2:MeasurementTimeseries>"
    "</wfs:FeatureCollection>";

// Minimal forecast XML with 5 values per parameter, 3 parameters
static const char* FORECAST_XML =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
    "<wfs:FeatureCollection>"
    "<wml2:MeasurementTimeseries gml:id=\"mts-1-WindSpeedMS\">"
    "<wml2:value>4.14</wml2:value>"
    "<wml2:value>3.99</wml2:value>"
    "<wml2:value>3.46</wml2:value>"
    "<wml2:value>4.19</wml2:value>"
    "<wml2:value>5.01</wml2:value>"
    "</wml2:MeasurementTimeseries>"
    "<wml2:MeasurementTimeseries gml:id=\"mts-1-WindGust\">"
    "<wml2:value>7.2</wml2:value>"
    "<wml2:value>6.8</wml2:value>"
    "<wml2:value>5.9</wml2:value>"
    "<wml2:value>7.1</wml2:value>"
    "<wml2:value>8.3</wml2:value>"
    "</wml2:MeasurementTimeseries>"
    "<wml2:MeasurementTimeseries gml:id=\"mts-1-WindDirection\">"
    "<wml2:value>225.0</wml2:value>"
    "<wml2:value>230.0</wml2:value>"
    "<wml2:value>240.0</wml2:value>"
    "<wml2:value>250.0</wml2:value>"
    "<wml2:value>255.0</wml2:value>"
    "</wml2:MeasurementTimeseries>"
    "</wfs:FeatureCollection>";

// ── Raw parser tests ──

void test_parse_obs_param_count(void) {
    FmiParseResult r = parseFmiResponse(OBS_XML, 0);
    TEST_ASSERT_TRUE(r.valid);
    TEST_ASSERT_EQUAL_INT(6, r.param_count);
}

void test_parse_obs_value_count(void) {
    FmiParseResult r = parseFmiResponse(OBS_XML, 0);
    for (int i = 0; i < 6; i++) {
        TEST_ASSERT_EQUAL_INT(3, r.params[i].count);
    }
}

void test_parse_obs_temp_values(void) {
    FmiParseResult r = parseFmiResponse(OBS_XML, 0);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.6f, r.params[0].values[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.4f, r.params[0].values[1]);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.0f, r.params[0].values[2]);
}

void test_parse_obs_last_value(void) {
    FmiParseResult r = parseFmiResponse(OBS_XML, 0);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.0f, fmiLastValue(r.params[0]));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 4.7f, fmiLastValue(r.params[1]));
}

void test_parse_forecast_param_count(void) {
    FmiParseResult r = parseFmiResponse(FORECAST_XML, 0);
    TEST_ASSERT_TRUE(r.valid);
    TEST_ASSERT_EQUAL_INT(3, r.param_count);
}

void test_parse_forecast_values(void) {
    FmiParseResult r = parseFmiResponse(FORECAST_XML, 0);
    TEST_ASSERT_EQUAL_INT(5, r.params[0].count);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 4.14f, r.params[0].values[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 5.01f, r.params[0].values[4]);
}

// ── NaN handling ──

void test_parse_nan_value(void) {
    const char* xml =
        "<wml2:MeasurementTimeseries gml:id=\"test\">"
        "<wml2:value>5.0</wml2:value>"
        "<wml2:value>NaN</wml2:value>"
        "<wml2:value>3.0</wml2:value>"
        "</wml2:MeasurementTimeseries>";

    FmiParseResult r = parseFmiResponse(xml, 0);
    TEST_ASSERT_EQUAL_INT(1, r.param_count);
    TEST_ASSERT_EQUAL_INT(3, r.params[0].count);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 5.0f, r.params[0].values[0]);
    TEST_ASSERT_TRUE(isnan(r.params[0].values[1]));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.0f, r.params[0].values[2]);
}

// ── Extract observations ──

void test_extract_observations(void) {
    FmiParseResult r = parseFmiResponse(OBS_XML, 0);
    FmiObservations obs = extractObservations(r);
    TEST_ASSERT_TRUE(obs.valid);
    // Last values: t2m=2.0, ws=4.7, wg=5.8, wd=225, rh=82, p=1012.8
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 2.0f, obs.temperature);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 4.7f, obs.wind_speed);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 5.8f, obs.wind_gust);
    TEST_ASSERT_EQUAL_INT(225, obs.wind_dir);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 82.0f, obs.humidity);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 1012.8f, obs.pressure);
}

void test_extract_observations_insufficient_params(void) {
    const char* xml =
        "<wml2:MeasurementTimeseries gml:id=\"test\">"
        "<wml2:value>5.0</wml2:value>"
        "</wml2:MeasurementTimeseries>";
    FmiParseResult r = parseFmiResponse(xml, 0);
    FmiObservations obs = extractObservations(r);
    TEST_ASSERT_FALSE(obs.valid);
}

// ── Extract forecast ──

void test_extract_forecast(void) {
    FmiParseResult r = parseFmiResponse(FORECAST_XML, 0);
    FmiForecastArrays fc = extractWindForecast(r);
    TEST_ASSERT_TRUE(fc.valid);
    TEST_ASSERT_EQUAL_INT(5, fc.count);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 4.14f, fc.wind[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 7.2f, fc.gust[0]);
    TEST_ASSERT_EQUAL_INT(225, fc.dir[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 8.3f, fc.gust[4]);
    TEST_ASSERT_EQUAL_INT(255, fc.dir[4]);
}

// ── Edge cases ──

void test_parse_empty_input(void) {
    FmiParseResult r = parseFmiResponse("", 0);
    TEST_ASSERT_FALSE(r.valid);
    TEST_ASSERT_EQUAL_INT(0, r.param_count);
}

void test_parse_null_input(void) {
    FmiParseResult r = parseFmiResponse(NULL, 0);
    TEST_ASSERT_FALSE(r.valid);
}

void test_parse_error_response(void) {
    const char* xml =
        "<?xml version=\"1.0\"?>"
        "<ExceptionReport>"
        "<Exception><ExceptionText>Error</ExceptionText></Exception>"
        "</ExceptionReport>";
    FmiParseResult r = parseFmiResponse(xml, 0);
    TEST_ASSERT_FALSE(r.valid);
    TEST_ASSERT_EQUAL_INT(0, r.param_count);
}

void test_parse_empty_timeseries(void) {
    const char* xml =
        "<wml2:MeasurementTimeseries gml:id=\"empty\">"
        "</wml2:MeasurementTimeseries>";
    FmiParseResult r = parseFmiResponse(xml, 0);
    TEST_ASSERT_EQUAL_INT(1, r.param_count);
    TEST_ASSERT_EQUAL_INT(0, r.params[0].count);
    TEST_ASSERT_TRUE(isnan(fmiLastValue(r.params[0])));
}

void test_parse_negative_values(void) {
    const char* xml =
        "<wml2:MeasurementTimeseries gml:id=\"test\">"
        "<wml2:value>-5.2</wml2:value>"
        "<wml2:value>-0.1</wml2:value>"
        "</wml2:MeasurementTimeseries>";
    FmiParseResult r = parseFmiResponse(xml, 0);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -5.2f, r.params[0].values[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -0.1f, r.params[0].values[1]);
}

static const char* MPC_OBS_15MIN =
    "<gmlcov:rangeType>\n"
    "  <swe:field name=\"ws_10min\"/>\n"
    "  <swe:field name=\"wg_pt10m_max\"/>\n"
    "  <swe:field name=\"wd_10min\"/>\n"
    "  <swe:field name=\"t2m\"/>\n"
    "</gmlcov:rangeType>\n"
    "<gmlcov:positions>\n"
    "60.10512 24.97539  1787202900\n"
    "                60.10512 24.97539  1787203800\n"
    "                60.10512 24.97539  1787204700\n"
    "                60.10512 24.97539  1787205600\n"
    "                60.10512 24.97539  1787206500\n"
    "                60.10512 24.97539  1787207400\n"
    "                60.10512 24.97539  1787208300\n"
    "                60.10512 24.97539  1787209200\n"
    "                60.10512 24.97539  1787210100\n"
    "</gmlcov:positions>\n"
    "<gml:doubleOrNilReasonTupleList>\n"
    "3.5 3.9 79.0 NaN \n"
    "                3.0 3.5 94.0 13.7 \n"
    "                1.9 2.9 101.0 NaN \n"
    "                2.6 4.0 140.0 14.5 \n"
    "                4.4 5.7 143.0 NaN \n"
    "                4.4 4.9 134.0 15.0 \n"
    "                4.9 5.9 136.0 NaN \n"
    "                4.8 5.5 128.0 15.8 \n"
    "                5.4 5.8 129.0 NaN\n"
    "</gml:doubleOrNilReasonTupleList>\n"
    "";

// ── multipointcoverage parser ──

void test_mpc_parses_real_observation_capture(void) {
    // Captured from fmi::observations::weather::multipointcoverage at Harmaja,
    // 15-minute steps: 4 parameters x 9 timesteps = the -2h -> NOW strip.
    FmiParseResult r = {};
    parseFmiMultipointInto(r, MPC_OBS_15MIN, 0);

    TEST_ASSERT_TRUE(r.valid);
    TEST_ASSERT_EQUAL_INT(4, r.param_count);
    TEST_ASSERT_EQUAL_INT(9, r.params[0].count);

    // Column order follows <swe:field>: ws_10min, wg_pt10m_max, wd_10min, t2m
    TEST_ASSERT_EQUAL_FLOAT(3.5f, r.params[0].values[0]);
    TEST_ASSERT_EQUAL_FLOAT(5.4f, r.params[0].values[8]);
    TEST_ASSERT_EQUAL_FLOAT(3.9f, r.params[1].values[0]);
    TEST_ASSERT_EQUAL_FLOAT(79.0f, r.params[2].values[0]);
}

void test_mpc_nan_preserved(void) {
    // Harmaja reports t2m every 30 min but wind every 15, so odd quarters are
    // NaN. Those must stay NaN, not become 0.
    FmiParseResult r = {};
    parseFmiMultipointInto(r, MPC_OBS_15MIN, 0);
    TEST_ASSERT_TRUE(isnan(r.params[3].values[0]));
    TEST_ASSERT_EQUAL_FLOAT(13.7f, r.params[3].values[1]);
    TEST_ASSERT_TRUE(isnan(r.params[3].values[2]));
}

void test_mpc_last_valid_value_skips_nan(void) {
    FmiParseResult r = {};
    parseFmiMultipointInto(r, MPC_OBS_15MIN, 0);
    // Final t2m sample is NaN; the last real reading is 15.8.
    TEST_ASSERT_TRUE(isnan(fmiLastValue(r.params[3])));
    TEST_ASSERT_EQUAL_FLOAT(15.8f, fmiLastValidValue(r.params[3]));
}

void test_mpc_timestamps(void) {
    FmiTimestamps ts = {};
    parseFmiTimestamps(ts, MPC_OBS_15MIN, 0);
    TEST_ASSERT_EQUAL_INT(9, ts.count);
    // 8 intervals of 15 min = exactly 2h from first to last.
    TEST_ASSERT_EQUAL_INT(900, ts.t[1] - ts.t[0]);
    TEST_ASSERT_EQUAL_INT(7200, ts.t[8] - ts.t[0]);
}

void test_mpc_rejects_garbage(void) {
    FmiParseResult r = {};
    parseFmiMultipointInto(r, "<html>not xml</html>", 0);
    TEST_ASSERT_FALSE(r.valid);
    parseFmiMultipointInto(r, NULL, 0);
    TEST_ASSERT_FALSE(r.valid);
}

void test_mpc_timestamps_absent_is_safe(void) {
    FmiTimestamps ts = {};
    parseFmiTimestamps(ts, "<html>no positions</html>", 0);
    TEST_ASSERT_EQUAL_INT(0, ts.count);
}


void test_extract_observations_skips_trailing_nan(void) {
    // Live Harmaja responses can end with the slower parameters unreported:
    //   "NaN 8.1 9.4 147.0 NaN NaN"
    // Taking the final sample verbatim blanked AIR on screen; extraction must
    // scan back to the last real reading per parameter.
    static const char* XML =
        "<gmlcov:rangeType>"
        "<swe:field name=\"t2m\"/><swe:field name=\"ws_10min\"/>"
        "<swe:field name=\"wg_pt10m_max\"/><swe:field name=\"wd_10min\"/>"
        "<swe:field name=\"rh\"/><swe:field name=\"p_sea\"/>"
        "</gmlcov:rangeType>"
        "<gml:doubleOrNilReasonTupleList>\n"
        "14.2 7.0 8.0 140.0 88.0 1001.0\n"
        "NaN 8.1 9.4 147.0 NaN NaN\n"
        "</gml:doubleOrNilReasonTupleList>";
    FmiParseResult r = {};
    parseFmiMultipointInto(r, XML, 0);
    TEST_ASSERT_TRUE(r.valid);

    FmiObservations o = extractObservations(r);
    TEST_ASSERT_TRUE(o.valid);
    TEST_ASSERT_EQUAL_FLOAT(14.2f, o.temperature);   // last valid, not NaN
    TEST_ASSERT_EQUAL_FLOAT(8.1f, o.wind_speed);     // newest sample
    TEST_ASSERT_EQUAL_INT(147, o.wind_dir);
    TEST_ASSERT_EQUAL_FLOAT(1001.0f, o.pressure);
}

void test_extract_obs_history(void) {
    FmiParseResult r = {};
    parseFmiMultipointInto(r, MPC_OBS_15MIN, 0);
    FmiObsHistory h = extractObsHistory(r);
    TEST_ASSERT_TRUE(h.valid);
    TEST_ASSERT_EQUAL_INT(9, h.count);   // 8 x 15min intervals = exactly 2h
    TEST_ASSERT_EQUAL_FLOAT(3.5f, h.wind[0]);
    TEST_ASSERT_EQUAL_FLOAT(5.4f, h.wind[8]);
    TEST_ASSERT_EQUAL_INT(129, h.dir[8]);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_parse_obs_param_count);
    RUN_TEST(test_parse_obs_value_count);
    RUN_TEST(test_parse_obs_temp_values);
    RUN_TEST(test_parse_obs_last_value);
    RUN_TEST(test_parse_forecast_param_count);
    RUN_TEST(test_parse_forecast_values);
    RUN_TEST(test_parse_nan_value);
    RUN_TEST(test_extract_observations);
    RUN_TEST(test_extract_observations_insufficient_params);
    RUN_TEST(test_extract_forecast);
    RUN_TEST(test_parse_empty_input);
    RUN_TEST(test_parse_null_input);
    RUN_TEST(test_parse_error_response);
    RUN_TEST(test_parse_empty_timeseries);
    RUN_TEST(test_parse_negative_values);
    RUN_TEST(test_mpc_parses_real_observation_capture);
    RUN_TEST(test_mpc_nan_preserved);
    RUN_TEST(test_mpc_last_valid_value_skips_nan);
    RUN_TEST(test_mpc_timestamps);
    RUN_TEST(test_mpc_rejects_garbage);
    RUN_TEST(test_mpc_timestamps_absent_is_safe);
    RUN_TEST(test_extract_observations_skips_trailing_nan);
    RUN_TEST(test_extract_obs_history);
    UNITY_END();
    return 0;
}
