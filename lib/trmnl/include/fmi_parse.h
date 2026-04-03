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

// Convenience: get the last (most recent) value from a timeseries.
// Returns NAN if timeseries is empty.
float fmiLastValue(const FmiTimeseries& ts);

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
    int   count;
    bool  valid;
};

// Extract forecast arrays from a parsed response.
// Assumes parameter order: WindSpeedMS, WindGust, WindDirection
FmiForecastArrays extractWindForecast(const FmiParseResult& parsed);
