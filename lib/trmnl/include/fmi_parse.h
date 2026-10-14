#pragma once

#include <cstddef>
#include <cmath>

// Maximum number of parameters per query
#define FMI_MAX_PARAMS 6

// Maximum number of time steps (hourly forecast can be ~66h)
#define FMI_MAX_TIMESTEPS 72

// Parsed result for one parameter's timeseries
struct FmiTimeseries {
    float values[FMI_MAX_TIMESTEPS];
    int count;       // number of values parsed
};

// Parsed result for a full FMI WFS response
struct FmiParseResult {
    FmiTimeseries params[FMI_MAX_PARAMS];
    int param_count;  // number of MeasurementTimeseries found
    bool valid;
};

// Parse an FMI WFS XML response (GML format).
// Extracts all <wml2:value> elements from each <wml2:MeasurementTimeseries>.
// Parameters are ordered by their appearance in the XML (same order as the query).
// NaN strings in the XML are stored as NAN float values.
//
// xml: null-terminated XML string
// len: length of xml (or 0 to use strlen)
FmiParseResult parseFmiResponse(const char* xml, size_t len);

// Same parse, writing straight into a caller-supplied result.
//
// FmiParseResult is 1760 bytes. Returning it by value and assigning
// (`buf = parseFmiResponse(...)`) builds a temporary in the callee's frame
// first — no NRVO, since that is copy-assignment rather than initialisation.
// Deep in the FMI fetch chain, alongside HTTPClient/TLS and a multi-MB
// response String, that overflowed the Arduino loop task on device: the
// watchdog killed it mid-fetch and the reset landed just after the sea-level
// request, which looked like a WiFi bug because WiFi teardown ran next.
//
// Firmware paths must use this form. parseFmiResponse() remains for the native
// tests, where stack depth is not a constraint.
void parseFmiResponseInto(FmiParseResult& result_out, const char* xml, size_t len);

// Parse an FMI multipointcoverage response.
//
// Same data as timevaluepair, ~7x smaller on the wire (81 KB vs 11 KB for a
// 48h/4-parameter forecast), which is radio-on time on a battery budget.
//
// Layout differs: one <gml:doubleOrNilReasonTupleList> holds every timestep as
// a whitespace-separated row, with the columns in <swe:field> order — so the
// parameters transpose into FmiParseResult's per-parameter series. A parallel
// <gmlcov:positions> block gives "lat lon <unix-epoch>" per row, so timestamps
// come free with no ISO-8601 parsing.
//
// Note the element is doubleOrNilReason*Tuple*List, not doubleOrNullValueList.
void parseFmiMultipointInto(FmiParseResult& result_out, const char* xml, size_t len);

// Timestamps from the multipointcoverage <gmlcov:positions> block, one per
// timestep, as Unix epoch seconds. Filled by parseFmiMultipointInto().
// Zero when the response carried no positions.
struct FmiTimestamps {
    int32_t t[FMI_MAX_TIMESTEPS];
    int count;
};

// Read the positions block into `out`. Safe to call on any response; leaves
// count 0 when absent.
void parseFmiTimestamps(FmiTimestamps& out, const char* xml, size_t len);

// Convenience: get the last (most recent) value from a timeseries.
// Returns NAN if timeseries is empty.
float fmiLastValue(const FmiTimeseries& ts);

// Last non-NaN value in a series, scanning backwards. Returns NAN if none.
// Needed because stations report some parameters less often than others —
// Harmaja gives wind every 15 min but temperature only every 30, so the
// co-indexed t2m sample is frequently NaN.
float fmiLastValidValue(const FmiTimeseries& ts);

// ── Observation result (extracted from a 6-parameter observation query) ──

struct FmiObservations {
    float temperature;   // °C
    float wind_speed;    // m/s
    float wind_gust;     // m/s
    int   wind_dir;      // degrees
    float humidity;      // %
    float pressure;      // hPa
    bool valid;
};

// Extract latest observations from a parsed response.
// Assumes parameter order: t2m, ws_10min, wg_pt10m_max, wd_10min, rh, p_sea
FmiObservations extractObservations(const FmiParseResult& parsed);

// ── Forecast extraction ──

struct FmiForecastArrays {
    float wind[FMI_MAX_TIMESTEPS];
    float gust[FMI_MAX_TIMESTEPS];
    int   dir[FMI_MAX_TIMESTEPS];
    float rain[FMI_MAX_TIMESTEPS];   // mm/h
    float air[FMI_MAX_TIMESTEPS];    // degC
    int   count;
    bool  valid;
    // Local hour of element 0, taken from the response's own timestamps. The
    // series does NOT begin at the current hour — HARMONIE returns whole hours
    // from the last model run, so element 0 is typically the next round hour.
    // Indexing by wall-clock hour instead skewed every lookup by the difference
    // (three hours in summer, since the timestamps are UTC).
    int   start_hour;
    // Whole days between today and element 0's local date. Usually 0, but 1
    // when the series begins after midnight — which is exactly the case the
    // flag-down rollover is for, so it is read from the data rather than
    // inferred a second time from the clock.
    int   start_day_offset;
};

// Extract forecast arrays from a parsed response.
// Parameter order: WindSpeedMS, WindGust, WindDirection, Precipitation1h,
// Temperature. Rain and air are optional — a 3-parameter response still parses,
// leaving those series NaN so their bars and summary ranges do not render.
FmiForecastArrays extractWindForecast(const FmiParseResult& parsed);

// Observation history for the -2h -> NOW strip.
// Parameter order: ws_10min, wg_pt10m_max, wd_10min, t2m
struct FmiObsHistory {
    float wind[FMI_MAX_TIMESTEPS];
    float gust[FMI_MAX_TIMESTEPS];
    int   dir[FMI_MAX_TIMESTEPS];
    int   count;
    bool  valid;
};

FmiObsHistory extractObsHistory(const FmiParseResult& parsed);
