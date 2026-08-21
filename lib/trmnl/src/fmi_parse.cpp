#include "fmi_parse.h"
#include "trmnl_log.h"
#include <cstring>
#include <cstdlib>
#include <cmath>

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

// By-value wrapper, kept for the native tests and any caller with stack to
// spare. Firmware paths should use parseFmiResponseInto() — see fmi_parse.h.
FmiParseResult parseFmiResponse(const char* xml, size_t len) {
    FmiParseResult result;
    parseFmiResponseInto(result, xml, len);
    return result;
}

void parseFmiResponseInto(FmiParseResult& result_out, const char* xml, size_t len) {
    FmiParseResult& result = result_out;
    result = FmiParseResult{};
    result.valid = false;

    if (!xml) {
        Log_error("parseFmiResponse: null xml input");
        return;
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

    // Scan back to the last real reading rather than taking the final sample:
    // stations report parameters at different cadences, so the newest row is
    // routinely NaN for the slower ones. Harmaja gives wind every 10 minutes
    // but temperature, humidity and pressure only every 30, and the live
    // response ends "NaN 8.1 9.4 147.0 NaN NaN" — taking it verbatim would
    // blank the AIR reading on screen.
    obs.temperature = fmiLastValidValue(parsed.params[0]);
    obs.wind_speed = fmiLastValidValue(parsed.params[1]);
    obs.wind_gust = fmiLastValidValue(parsed.params[2]);
    float dir = fmiLastValidValue(parsed.params[3]);
    obs.wind_dir = std::isnan(dir) ? -1 : (int)dir;
    obs.humidity = fmiLastValidValue(parsed.params[4]);
    obs.pressure = fmiLastValidValue(parsed.params[5]);
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
        fc.dir[i] = std::isnan(dir_ts.values[i]) ? 0 : (int)dir_ts.values[i];
        // Optional series stay NaN when the query did not ask for them, which
        // reads downstream as "no data" rather than "zero rain / 0 degC".
        fc.rain[i] = NAN;
        fc.air[i]  = NAN;
    }
    if (parsed.param_count >= 4) {
        const FmiTimeseries& rain_ts = parsed.params[3];
        for (int i = 0; i < fc.count && i < rain_ts.count; i++) fc.rain[i] = rain_ts.values[i];
    }
    if (parsed.param_count >= 5) {
        const FmiTimeseries& air_ts = parsed.params[4];
        for (int i = 0; i < fc.count && i < air_ts.count; i++) fc.air[i] = air_ts.values[i];
    }

    fc.valid = (fc.count > 0);
    Log_info("FMI wind forecast: %d hours, valid=%d", fc.count, fc.valid);
    return fc;
}


// ── multipointcoverage ──

// Count the <swe:field name="..."> entries, which give the column order of the
// tuple list. Returns 0 when the block is absent.
static int countSweFields(const char* xml, const char* end) {
    static const char* FIELD = "<swe:field name=";
    int n = 0;
    const char* p = xml;
    while ((p = strstr(p, FIELD)) != NULL && p < end) {
        n++;
        p += 16;
    }
    return n;
}

void parseFmiMultipointInto(FmiParseResult& result_out, const char* xml, size_t len) {
    FmiParseResult& result = result_out;
    result = FmiParseResult{};
    result.valid = false;

    if (!xml) {
        Log_error("parseFmiMultipoint: null xml input");
        return;
    }
    if (len == 0) len = strlen(xml);

    static const char* TUPLE_OPEN  = "<gml:doubleOrNilReasonTupleList>";
    static const char* TUPLE_CLOSE = "</gml:doubleOrNilReasonTupleList>";

    const char* xml_end = xml + len;
    const char* open = strstr(xml, TUPLE_OPEN);
    if (!open) {
        Log_error("parseFmiMultipoint: no tuple list in %zu bytes", len);
        return;
    }
    const char* data = open + strlen(TUPLE_OPEN);
    const char* close = strstr(data, TUPLE_CLOSE);
    if (!close) {
        Log_error("parseFmiMultipoint: unterminated tuple list");
        return;
    }

    int ncols = countSweFields(xml, xml_end);
    if (ncols <= 0 || ncols > FMI_MAX_PARAMS) {
        Log_error("parseFmiMultipoint: %d fields (max %d)", ncols, FMI_MAX_PARAMS);
        return;
    }
    result.param_count = ncols;

    // Walk whitespace-separated tokens, filling column-major. Row r column c
    // lands in params[c].values[r] — the tuple list is row-major, so this is
    // the transpose that makes it look like a timevaluepair parse downstream.
    int col = 0, row = 0;
    const char* p = data;
    while (p < close && row < FMI_MAX_TIMESTEPS) {
        while (p < close && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) p++;
        if (p >= close) break;
        const char* tok = p;
        while (p < close && *p != ' ' && *p != '\n' && *p != '\r' && *p != '\t') p++;

        float v = parseValue(tok, p);
        result.params[col].values[row] = v;
        if (row + 1 > result.params[col].count) result.params[col].count = row + 1;

        if (++col >= ncols) { col = 0; row++; }
    }

    result.valid = (row > 0);
    if (result.valid) {
        Log_info("FMI multipoint: %d params x %d steps", ncols, row);
    } else {
        Log_error("parseFmiMultipoint: no rows parsed");
    }
}

void parseFmiTimestamps(FmiTimestamps& out, const char* xml, size_t len) {
    out = FmiTimestamps{};
    if (!xml) return;
    if (len == 0) len = strlen(xml);

    static const char* POS_OPEN  = "<gmlcov:positions>";
    static const char* POS_CLOSE = "</gmlcov:positions>";

    const char* open = strstr(xml, POS_OPEN);
    if (!open) return;
    const char* data = open + strlen(POS_OPEN);
    const char* close = strstr(data, POS_CLOSE);
    if (!close) return;

    // Each line is "lat lon epoch"; we want the third token of each.
    const char* p = data;
    int tok_in_row = 0;
    while (p < close && out.count < FMI_MAX_TIMESTEPS) {
        while (p < close && (*p == ' ' || *p == '\t')) p++;
        if (p < close && (*p == '\n' || *p == '\r')) { p++; tok_in_row = 0; continue; }
        if (p >= close) break;
        const char* tok = p;
        while (p < close && *p != ' ' && *p != '\n' && *p != '\r' && *p != '\t') p++;

        if (tok_in_row == 2) {
            char buf[16];
            size_t n = (size_t)(p - tok);
            if (n >= sizeof(buf)) n = sizeof(buf) - 1;
            memcpy(buf, tok, n);
            buf[n] = '\0';
            out.t[out.count++] = (int32_t)atol(buf);
        }
        tok_in_row++;
    }
}

float fmiLastValidValue(const FmiTimeseries& ts) {
    for (int i = ts.count - 1; i >= 0; i--) {
        if (!std::isnan(ts.values[i])) return ts.values[i];
    }
    return NAN;
}

FmiObsHistory extractObsHistory(const FmiParseResult& parsed) {
    FmiObsHistory h = {};
    h.valid = false;

    // Expected parameter order: ws_10min, wg_pt10m_max, wd_10min[, t2m]
    if (parsed.param_count < 3) {
        Log_error("extractObsHistory: need 3 params, got %d", parsed.param_count);
        return h;
    }
    const FmiTimeseries& wind_ts = parsed.params[0];
    const FmiTimeseries& gust_ts = parsed.params[1];
    const FmiTimeseries& dir_ts  = parsed.params[2];

    h.count = wind_ts.count;
    if (gust_ts.count < h.count) h.count = gust_ts.count;
    if (dir_ts.count  < h.count) h.count = dir_ts.count;

    for (int i = 0; i < h.count; i++) {
        h.wind[i] = wind_ts.values[i];
        h.gust[i] = gust_ts.values[i];
        h.dir[i]  = std::isnan(dir_ts.values[i]) ? 0 : (int)dir_ts.values[i];
    }
    h.valid = (h.count > 0);
    Log_info("FMI obs history: %d steps, valid=%d", h.count, h.valid);
    return h;
}
