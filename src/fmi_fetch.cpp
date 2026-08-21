#include "fmi_fetch.h"

#ifdef CLOCK91_MODE

#include <Arduino.h>
#include <HTTPClient.h>
#include <trmnl_log.h>
#include "fmi_parse.h"

static const char* FMI_BASE = "http://opendata.fmi.fi/wfs"
    "?service=WFS&version=2.0.0&request=getFeature";

// Fetch a URL and return the response body, or empty string on failure.
static String fmiHttpGet(const char* url) {
    HTTPClient http;
    http.setTimeout(15000);
    http.setConnectTimeout(10000);

    Log_info("FMI fetch: %s", url);

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

// Shared scratch for parsed responses.
//
// FmiParseResult is 1760 bytes. It used to be a local in each fetch function,
// so the parse result, the HTTPClient, and the multi-megabyte response String
// were all live on the Arduino loop task's stack at once — deep in a call
// chain that already includes WiFi and TLS. On device that overflowed and the
// task was killed by the watchdog partway through the FMI fetches ("PRO CPU
// has been reset by WDT" right after the sea-level request), which looked like
// a WiFi teardown bug because teardown was simply what ran next.
//
// One shared buffer is safe here: the fetches are strictly sequential on a
// single task and nothing holds a reference past the extract call. Not
// reentrant — if FMI fetching ever moves to its own task this needs revisiting.
static FmiParseResult g_parsed;

// ISO-8601 UTC timestamp `offset_sec` from now, written into `out`.
static void isoUtcOffset(char* out, size_t len, int offset_sec) {
    time_t t = time(NULL) + offset_sec;
    struct tm tm_utc;
    gmtime_r(&t, &tm_utc);
    strftime(out, len, "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
}

// Shared URL scratch.
//
// These queries used to be built with chained `String + "..."` expressions.
// Every `+` materialises a StringSumHelper temporary on the stack, and this
// runs deep in a chain that already holds WiFi, HTTPClient and TLS buffers —
// the trace in clock91.cpp reported 380 bytes of headroom at entry, and adding
// two more temporaries for the starttime bound was enough to overflow it and
// panic with LoadProhibited inside String::changeBuffer.
//
// snprintf into one fixed buffer keeps the whole URL off the stack. Same
// single-task, strictly-sequential reasoning as g_parsed below: not reentrant.
static char g_url[512];

FmiObservations fmiFetchObservations(const FmiStation& station) {
    FmiObservations obs = {};
    obs.valid = false;

    // Bounded to one hour: unbounded this returns 24h at 10-minute steps — 719
    // rows and ~70 KB for the handful of current values we use, and more rows
    // than FMI_MAX_TIMESTEPS can hold. An hour still survives a station
    // reporting its slower parameters only every 30 minutes.
    char start[24];
    isoUtcOffset(start, sizeof(start), -3600);
    snprintf(g_url, sizeof(g_url),
             "%s&storedquery_id=fmi::observations::weather::multipointcoverage"
             "&fmisid=%s"
             "&parameters=t2m,ws_10min,wg_pt10m_max,wd_10min,rh,p_sea"
             "&starttime=%s&timestep=10",
             FMI_BASE, station.fmisid, start);

    String body = fmiHttpGet(g_url);
    if (body.length() == 0) return obs;

    parseFmiMultipointInto(g_parsed, body.c_str(), body.length());
    if (!g_parsed.valid) return obs;

    obs = extractObservations(g_parsed);
    return obs;
}

FmiForecastArrays fmiFetchWindForecast(const FmiStation& station) {
    FmiForecastArrays fc = {};
    fc.valid = false;

    // Query by fmisid, not latlon. The HARMONIE stored query accepts both
    // (describeStoredQueries lists place/latlon/geoid/fmisid/wmo), but latlon
    // resolves to whatever model point is nearest the coordinates we happen to
    // have in the station table, while fmisid asks for the forecast at the
    // station itself. Same answer today for Itätoukki — identical to two
    // decimals — but it removes a source of drift if a table coordinate is ever
    // slightly off, and it keeps the forecast and the observations pinned to one
    // identity.
    snprintf(g_url, sizeof(g_url),
             "%s&storedquery_id=fmi::forecast::harmonie::surface::point::multipointcoverage"
             "&fmisid=%s"
             "&parameters=WindSpeedMS,WindGust,WindDirection,Precipitation1h,Temperature"
             "&timestep=60",
             FMI_BASE, station.fmisid);

    String body = fmiHttpGet(g_url);
    if (body.length() == 0) return fc;

    parseFmiMultipointInto(g_parsed, body.c_str(), body.length());
    if (!g_parsed.valid) return fc;

    fc = extractWindForecast(g_parsed);

    // Anchor the series to its own first timestamp, converted to local time,
    // rather than assuming it starts at the current hour.
    FmiTimestamps ts;
    parseFmiTimestamps(ts, body.c_str(), body.length());
    if (ts.count > 0) {
        time_t t0 = (time_t)ts.t[0];
        struct tm lt;
        localtime_r(&t0, &lt);
        fc.start_hour = lt.tm_hour;

        // How many whole days ahead element 0 is. Compare local calendar days
        // rather than subtracting epochs, so DST changes cannot skew it.
        time_t now = time(NULL);
        struct tm nt;
        localtime_r(&now, &nt);
        struct tm a = nt, b = lt;
        a.tm_hour = a.tm_min = a.tm_sec = 0; a.tm_isdst = -1;
        b.tm_hour = b.tm_min = b.tm_sec = 0; b.tm_isdst = -1;
        double days = difftime(mktime(&b), mktime(&a)) / 86400.0;
        fc.start_day_offset = (int)(days + 0.5);
        if (fc.start_day_offset < 0) fc.start_day_offset = 0;
    } else {
        // No positions block: fall back to the current hour, which is what the
        // old behaviour assumed.
        time_t now = time(NULL);
        struct tm lt;
        localtime_r(&now, &lt);
        fc.start_hour = lt.tm_hour;
        fc.start_day_offset = 0;
        Log_error("FMI forecast: no timestamps, anchoring to current hour");
    }
    return fc;
}

int fmiFetchSeaLevel(const FmiStation& station, int* out_sea, int max_hours) {
    // The oaas:: form of this query no longer exists — FMI returns
    // "No handler for ..." (HTTP 400). The live name drops that segment.
    snprintf(g_url, sizeof(g_url),
             "%s&storedquery_id=fmi::forecast::sealevel::point::multipointcoverage"
             "&fmisid=%s&parameters=SeaLevel",
             FMI_BASE, station.fmisid);

    String body = fmiHttpGet(g_url);
    if (body.length() == 0) return 0;

    parseFmiMultipointInto(g_parsed, body.c_str(), body.length());
    if (!g_parsed.valid || g_parsed.param_count < 1) return 0;

    // The query answers with two series regardless of what is asked for:
    // SeaLevel (theoretical mean water) then SeaLevelN2000. Both are valid
    // datums and both swing either side of zero — Finland is mid-transition
    // from N60 to N2000 — but the display quotes the older theoretical mean,
    // so take params[0].
    const FmiTimeseries& ts = g_parsed.params[0];
    int count = (ts.count < max_hours) ? ts.count : max_hours;

    for (int i = 0; i < count; i++) {
        out_sea[i] = std::isnan(ts.values[i]) ? 0 : (int)ts.values[i];
    }

    Log_info("FMI sea level: %d hours (N2000=%d)", count, g_parsed.param_count >= 2);
    return count;
}

FmiObsHistory fmiFetchObsHistory(const FmiStation& station, time_t now) {
    FmiObsHistory h = {};
    h.valid = false;

    // The top band's strip is -2h -> NOW: 8 intervals of 15 minutes, so 9
    // sample points including both endpoints. Ask for exactly that window,
    // rounded down to a quarter so the last sample lands on a real observation
    // rather than a partial interval.
    time_t end   = now - (now % 900);
    time_t start = end - 2 * 3600;

    struct tm s_tm, e_tm;
    gmtime_r(&start, &s_tm);
    gmtime_r(&end, &e_tm);
    char s_buf[24], e_buf[24];
    strftime(s_buf, sizeof(s_buf), "%Y-%m-%dT%H:%M:%SZ", &s_tm);
    strftime(e_buf, sizeof(e_buf), "%Y-%m-%dT%H:%M:%SZ", &e_tm);

    snprintf(g_url, sizeof(g_url),
             "%s&storedquery_id=fmi::observations::weather::multipointcoverage"
             "&fmisid=%s&parameters=ws_10min,wg_pt10m_max,wd_10min"
             "&starttime=%s&endtime=%s&timestep=15",
             FMI_BASE, station.fmisid, s_buf, e_buf);

    String body = fmiHttpGet(g_url);
    if (body.length() == 0) return h;

    parseFmiMultipointInto(g_parsed, body.c_str(), body.length());
    if (!g_parsed.valid) return h;

    h = extractObsHistory(g_parsed);
    return h;
}

#endif // CLOCK91_MODE
