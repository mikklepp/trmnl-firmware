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
    UNITY_END();
    return 0;
}
