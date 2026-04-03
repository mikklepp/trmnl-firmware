#include "fmi_parse.h"
#include "trmnl_log.h"
#include <cstring>
#include <cstdlib>

// Simple XML scanning — no DOM, no SAX library.
// Finds <wml2:MeasurementTimeseries> blocks and extracts <wml2:value> elements.

static const char* findStr(const char* haystack, const char* needle, const char* end) {
    size_t nlen = strlen(needle);
    for (const char* p = haystack; p + nlen <= end; p++) {
        if (memcmp(p, needle, nlen) == 0) return p;
    }
    return NULL;
}

static float parseValue(const char* start, const char* end) {
    // Check for "NaN" (FMI uses this for missing values)
    if (end - start == 3 && memcmp(start, "NaN", 3) == 0) {
        return NAN;
    }
    // Parse float from the substring
    char buf[32];
    size_t len = end - start;
    if (len >= sizeof(buf)) len = sizeof(buf) - 1;
    memcpy(buf, start, len);
    buf[len] = '\0';
    return (float)atof(buf);
}

FmiParseResult parseFmiResponse(const char* xml, size_t len) {
    FmiParseResult result = {};
    result.valid = false;

    if (!xml) {
        Log_error("parseFmiResponse: null xml input");
        return result;
    }
    if (len == 0) len = strlen(xml);
    Log_verbose("parseFmiResponse: parsing %zu bytes", len);

    const char* end = xml + len;
    const char* pos = xml;

    static const char* TS_OPEN = "<wml2:MeasurementTimeseries ";
    static const char* TS_CLOSE = "</wml2:MeasurementTimeseries>";
    static const char* VAL_OPEN = "<wml2:value>";
    static const char* VAL_CLOSE = "</wml2:value>";
    static const size_t VAL_OPEN_LEN = 12;
    static const size_t VAL_CLOSE_LEN = 13;

    while (result.param_count < FMI_MAX_PARAMS) {
        // Find next opening <wml2:MeasurementTimeseries ...>
        const char* ts_start = findStr(pos, TS_OPEN, end);
        if (!ts_start) break;

        // Find the closing </wml2:MeasurementTimeseries> as boundary
        const char* ts_close = findStr(ts_start + strlen(TS_OPEN), TS_CLOSE, end);
        const char* ts_end = ts_close ? ts_close : end;

        FmiTimeseries& ts = result.params[result.param_count];
        ts.count = 0;

        // Extract all <wml2:value>...</wml2:value> within this timeseries
        const char* vpos = ts_start;
        while (ts.count < FMI_MAX_TIMESTEPS) {
            const char* val_start = findStr(vpos, VAL_OPEN, ts_end);
            if (!val_start) break;
            val_start += VAL_OPEN_LEN;

            const char* val_end = findStr(val_start, VAL_CLOSE, ts_end);
            if (!val_end) break;

            ts.values[ts.count++] = parseValue(val_start, val_end);
            vpos = val_end + VAL_CLOSE_LEN;
        }

        result.param_count++;

        // Advance past the closing tag
        pos = ts_close ? ts_close + strlen(TS_CLOSE) : ts_end;
    }

    result.valid = (result.param_count > 0);
    if (result.valid) {
        Log_info("FMI parsed: %d params", result.param_count);
        for (int i = 0; i < result.param_count; i++) {
            Log_verbose("  param[%d]: %d values", i, result.params[i].count);
        }
    } else {
        Log_error("FMI parse failed: no timeseries found in %zu bytes", len);
    }
    return result;
}

float fmiLastValue(const FmiTimeseries& ts) {
    if (ts.count <= 0) return NAN;
    return ts.values[ts.count - 1];
}

FmiObservations extractObservations(const FmiParseResult& parsed) {
    FmiObservations obs = {};
    obs.valid = false;

    // Expected parameter order: t2m, ws_10min, wg_pt10m_max, wd_10min, rh, p_sea
    if (parsed.param_count < 6) {
        Log_error("extractObservations: need 6 params, got %d", parsed.param_count);
        return obs;
    }

    obs.temperature = fmiLastValue(parsed.params[0]);
    obs.wind_speed = fmiLastValue(parsed.params[1]);
    obs.wind_gust = fmiLastValue(parsed.params[2]);
    obs.wind_dir = isnan(fmiLastValue(parsed.params[3])) ? -1 : (int)fmiLastValue(parsed.params[3]);
    obs.humidity = fmiLastValue(parsed.params[4]);
    obs.pressure = fmiLastValue(parsed.params[5]);
    obs.valid = true;
    Log_info("FMI obs: %.1fC wind=%.1f gust=%.1f dir=%d hum=%.0f pres=%.0f",
             obs.temperature, obs.wind_speed, obs.wind_gust,
             obs.wind_dir, obs.humidity, obs.pressure);
    return obs;
}

FmiForecastArrays extractWindForecast(const FmiParseResult& parsed) {
    FmiForecastArrays fc = {};
    fc.valid = false;

    // Expected parameter order: WindSpeedMS, WindGust, WindDirection
    if (parsed.param_count < 3) {
        Log_error("extractWindForecast: need 3 params, got %d", parsed.param_count);
        return fc;
    }

    const FmiTimeseries& wind_ts = parsed.params[0];
    const FmiTimeseries& gust_ts = parsed.params[1];
    const FmiTimeseries& dir_ts = parsed.params[2];

    // Use the minimum count across all three
    fc.count = wind_ts.count;
    if (gust_ts.count < fc.count) fc.count = gust_ts.count;
    if (dir_ts.count < fc.count) fc.count = dir_ts.count;

    for (int i = 0; i < fc.count; i++) {
        fc.wind[i] = wind_ts.values[i];
        fc.gust[i] = gust_ts.values[i];
        fc.dir[i] = isnan(dir_ts.values[i]) ? 0 : (int)dir_ts.values[i];
    }

    fc.valid = (fc.count > 0);
    Log_info("FMI wind forecast: %d hours, valid=%d", fc.count, fc.valid);
    return fc;
}
