#include "fmi_fetch.h"

#ifdef CLOCK91_MODE

#include <Arduino.h>
#include <HTTPClient.h>
#include <trmnl_log.h>
#include "fmi_parse.h"

static const char* FMI_BASE = "http://opendata.fmi.fi/wfs"
    "?service=WFS&version=2.0.0&request=getFeature";

// Fetch a URL and return the response body, or empty string on failure.
static String fmiHttpGet(const String& url) {
    HTTPClient http;
    http.setTimeout(15000);
    http.setConnectTimeout(10000);

    Log_info("FMI fetch: %s", url.c_str());

    if (!http.begin(url)) {
        Log_error("FMI: http.begin failed");
        return "";
    }

    int code = http.GET();
    if (code != 200) {
        Log_error("FMI: HTTP %d", code);
        http.end();
        return "";
    }

    String body = http.getString();
    http.end();

    Log_info("FMI: got %d bytes", body.length());
    return body;
}

FmiObservations fmiFetchObservations(const FmiStation& station) {
    FmiObservations obs = {};
    obs.valid = false;

    String url = String(FMI_BASE)
        + "&storedquery_id=fmi::observations::weather::timevaluepair"
        + "&fmisid=" + station.fmisid
        + "&parameters=t2m,ws_10min,wg_pt10m_max,wd_10min,rh,p_sea";

    String body = fmiHttpGet(url);
    if (body.length() == 0) return obs;

    FmiParseResult parsed = parseFmiResponse(body.c_str(), body.length());
    if (!parsed.valid) return obs;

    obs = extractObservations(parsed);
    return obs;
}

FmiForecastArrays fmiFetchWindForecast(const FmiStation& station) {
    FmiForecastArrays fc = {};
    fc.valid = false;

    // HARMONIE forecast uses lat/lon, not fmisid
    char latlon[32];
    snprintf(latlon, sizeof(latlon), "%.3f,%.3f", station.lat, station.lon);

    String url = String(FMI_BASE)
        + "&storedquery_id=fmi::forecast::harmonie::surface::point::timevaluepair"
        + "&latlon=" + latlon
        + "&parameters=WindSpeedMS,WindGust,WindDirection"
        + "&timestep=60";

    String body = fmiHttpGet(url);
    if (body.length() == 0) return fc;

    FmiParseResult parsed = parseFmiResponse(body.c_str(), body.length());
    if (!parsed.valid) return fc;

    fc = extractWindForecast(parsed);
    return fc;
}

int fmiFetchSeaLevel(const FmiStation& station, int* out_sea, int max_hours) {
    String url = String(FMI_BASE)
        + "&storedquery_id=fmi::forecast::oaas::sealevel::point::timevaluepair"
        + "&fmisid=" + station.fmisid
        + "&parameters=SeaLevel";

    String body = fmiHttpGet(url);
    if (body.length() == 0) return 0;

    FmiParseResult parsed = parseFmiResponse(body.c_str(), body.length());
    if (!parsed.valid || parsed.param_count < 1) return 0;

    const FmiTimeseries& ts = parsed.params[0];
    int count = (ts.count < max_hours) ? ts.count : max_hours;

    for (int i = 0; i < count; i++) {
        out_sea[i] = std::isnan(ts.values[i]) ? 0 : (int)ts.values[i];
    }

    Log_info("FMI sea level: %d hours", count);
    return count;
}

#endif // CLOCK91_MODE
