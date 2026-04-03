#include "clock91.h"

#ifdef CLOCK91_MODE

#include <Arduino.h>
#include <WiFi.h>
#include <WifiCaptive.h>
#include <Preferences.h>
#include <trmnl_log.h>
#include <bl.h>
#include <config.h>
#include "layout.h"
#include "render.h"
#include "stations.h"
#include "fmi_fetch.h"
#include "fmi_parse.h"
#include "forecast.h"
#include "sunset.h"
#include "alarm.h"

extern Preferences preferences;

// Europe/Helsinki: UTC+2 (winter) / UTC+3 (summer)
static const char* TIMEZONE = "EET-2EEST,M3.5.0/3,M10.5.0/4";

static int station_index = 0;

static bool clock91_wifi_connect(void) {
    WiFi.mode(WIFI_STA);

    if (!WifiCaptivePortal.isSaved()) {
        Log_info("clock91: no WiFi credentials, starting portal");
        WifiCaptivePortal.startPortal();
        if (WiFi.status() != WL_CONNECTED) {
            Log_error("clock91: portal failed, no WiFi");
            return false;
        }
    } else {
        int res = WifiCaptivePortal.autoConnect();
        if (!res) {
            Log_error("clock91: WiFi connect failed (status=%d)", WiFi.status());
            return false;
        }
    }

    Log_info("clock91: WiFi connected, IP=%s RSSI=%d",
             WiFi.localIP().toString().c_str(), WiFi.RSSI());
    return true;
}

static bool clock91_sync_time(void) {
    setenv("TZ", TIMEZONE, 1);
    tzset();

    configTime(0, 0, "time.google.com", "time.cloudflare.com");

    struct tm timeinfo = {};
    for (int i = 0; i < 50; i++) {
        if (getLocalTime(&timeinfo, 100)) {
            Log_info("clock91: NTP synced: %04d-%02d-%02d %02d:%02d:%02d",
                     timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                     timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
            return true;
        }
    }

    time_t now = time(NULL);
    Log_error("clock91: NTP sync failed, using RTC time");
    return (now > 1704067200);
}

static void clock91_fetch_fmi(const FmiStation& station, DisplayState& s,
                               ForecastGrid& grid) {
    // Observations
    FmiObservations obs = fmiFetchObservations(station);
    if (obs.valid) {
        s.wind_speed = obs.wind_speed;
        s.wind_gust = obs.wind_gust;
        s.wind_dir = obs.wind_dir;
        s.station_name = station.name;
    }

    // Wind forecast
    FmiForecastArrays fc = fmiFetchWindForecast(station);
    if (fc.valid) {
        struct tm ti;
        time_t now = time(NULL);
        localtime_r(&now, &ti);
        int current_hour = ti.tm_hour;

        aggregateWindForecast(&grid,
            fc.wind, fc.gust, fc.dir,
            fc.count, current_hour);
    }

    // Sea level forecast
    int sea_hours[FMI_MAX_TIMESTEPS];
    int sea_count = fmiFetchSeaLevel(station, sea_hours, FMI_MAX_TIMESTEPS);
    if (sea_count > 0) {
        aggregateSeaLevel(&grid, sea_hours, sea_count);
    }
}

static DisplayState clock91_build_state(void) {
    DisplayState s = {};

    struct tm ti;
    time_t now = time(NULL);
    localtime_r(&now, &ti);

    s.hour = ti.tm_hour;
    s.minute = ti.tm_min;
    s.day = ti.tm_mday;
    s.month = ti.tm_mon + 1;
    s.wday = ti.tm_wday;

    // Defaults — will show as dashes until BLE data fills them in
    s.solar_w = NAN;
    s.charger_w = NAN;
    s.battery_w = NAN;
    s.soc_pct = 0;
    s.engine_v = NAN;
    s.device_pct = 0;
    s.wind_speed = NAN;
    s.wind_gust = NAN;
    s.wind_dir = 0;
    s.saloon_temp = NAN;
    s.saloon_humidity = NAN;
    s.icebox_temp = NAN;
    s.alarm_valid = false;
    s.timer_active = false;
    s.station_name = NULL;

    return s;
}

static void clock91_compute_alarm(const FmiStation& station, struct tm& ti,
                                   DisplayState& s) {
    // Compute UTC offset from local vs UTC time
    time_t now = time(NULL);
    struct tm utc_tm;
    gmtime_r(&now, &utc_tm);
    int utc_offset_sec = (ti.tm_hour - utc_tm.tm_hour) * 3600
                       + (ti.tm_min - utc_tm.tm_min) * 60;
    // Handle day boundary
    if (utc_offset_sec > 43200) utc_offset_sec -= 86400;
    if (utc_offset_sec < -43200) utc_offset_sec += 86400;
    float utc_offset = utc_offset_sec / 3600.0f;

    SunTimes sun = calculateSunTimes(
        ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday,
        station.lat, station.lon);

    if (sun.valid) {
        int sunset_h, sunset_m;
        utcToLocal(sun.sunset_hours, utc_offset, &sunset_h, &sunset_m);

        AlarmState alarm = calculateAlarm(sunset_h, sunset_m,
                                          ti.tm_hour, ti.tm_min);
        s.alarm_hour = alarm.hour;
        s.alarm_minute = alarm.minute;
        s.alarm_valid = true;
    } else {
        // Polar night/midnight sun — use curfew only
        AlarmState alarm = calculateAlarm(-1, 0, ti.tm_hour, ti.tm_min);
        s.alarm_hour = alarm.hour;
        s.alarm_minute = alarm.minute;
        s.alarm_valid = true;
    }
}

void clock91_cycle(void) {
    Log_info("clock91: wake cycle start");

    render_init();

    // Load station index from NVS
    station_index = preferences.getUInt("station_idx", 0);
    if (station_index >= STATION_COUNT) station_index = 0;
    const FmiStation& station = STATIONS[station_index];
    Log_info("clock91: station %d/%d: %s (fmisid=%s)",
             station_index, STATION_COUNT, station.name, station.fmisid);

    bool wifi_ok = clock91_wifi_connect();

    if (wifi_ok) {
        clock91_sync_time();
    }

    // Build base state from clock
    DisplayState state = clock91_build_state();
    ForecastGrid grid = {};
    state.forecast = &grid;

    // Fetch FMI data if WiFi is up
    if (wifi_ok) {
        clock91_fetch_fmi(station, state, grid);
    }

    // Compute alarm from sunset
    struct tm ti;
    time_t now = time(NULL);
    localtime_r(&now, &ti);
    clock91_compute_alarm(station, ti, state);

    // Render
    DrawList dl = buildLayout(state);
    renderFull(dl);

    // Disconnect WiFi before sleep
    if (wifi_ok) {
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
    }

    Log_info("clock91: wake cycle done");
}

#endif // CLOCK91_MODE
