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

// ── WiFi ──

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

// ── NTP ──

static bool clock91_sync_time(void) {
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

    Log_error("clock91: NTP sync failed");
    return false;
}

// ── FMI fetch ──

static void clock91_fetch_fmi(const FmiStation& station, DisplayState& s,
                               ForecastGrid& grid) {
    FmiObservations obs = fmiFetchObservations(station);
    if (obs.valid) {
        s.wind_speed = obs.wind_speed;
        s.wind_gust = obs.wind_gust;
        s.wind_dir = obs.wind_dir;
    }

    FmiForecastArrays fc = fmiFetchWindForecast(station);
    if (fc.valid) {
        struct tm ti;
        time_t now = time(NULL);
        localtime_r(&now, &ti);
        aggregateWindForecast(&grid,
            fc.wind, fc.gust, fc.dir,
            fc.count, ti.tm_hour);
    }

    int sea_hours[FMI_MAX_TIMESTEPS];
    int sea_count = fmiFetchSeaLevel(station, sea_hours, FMI_MAX_TIMESTEPS);
    if (sea_count > 0) {
        aggregateSeaLevel(&grid, sea_hours, sea_count);
    }
}

// ── Alarm ──

static void clock91_compute_alarm(const FmiStation& station, struct tm& ti,
                                   DisplayState& s) {
    time_t now = time(NULL);
    struct tm utc_tm;
    gmtime_r(&now, &utc_tm);
    int utc_offset_sec = (ti.tm_hour - utc_tm.tm_hour) * 3600
                       + (ti.tm_min - utc_tm.tm_min) * 60;
    if (utc_offset_sec > 43200) utc_offset_sec -= 86400;
    if (utc_offset_sec < -43200) utc_offset_sec += 86400;
    float utc_offset = utc_offset_sec / 3600.0f;

    SunTimes sun = calculateSunTimes(
        ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday,
        station.lat, station.lon);

    int sunset_h = -1, sunset_m = 0;
    if (sun.valid) {
        utcToLocal(sun.sunset_hours, utc_offset, &sunset_h, &sunset_m);
    }

    AlarmState alarm = calculateAlarm(sunset_h, sunset_m,
                                      ti.tm_hour, ti.tm_min);
    s.alarm_hour = alarm.hour;
    s.alarm_minute = alarm.minute;
    s.alarm_valid = true;
}

// ── Full cycle: WiFi + NTP + FMI + full refresh ──

static void clock91_full_cycle(void) {
    int station_index = preferences.getUInt("station_idx", 0);
    if (station_index >= STATION_COUNT) station_index = 0;
    const FmiStation& station = STATIONS[station_index];

    bool wifi_ok = clock91_wifi_connect();
    if (wifi_ok) {
        clock91_sync_time();
    }

    struct tm ti;
    time_t now = time(NULL);
    localtime_r(&now, &ti);

    DisplayState state = {};
    ForecastGrid grid = {};

    state.hour = ti.tm_hour;
    state.minute = ti.tm_min;
    state.day = ti.tm_mday;
    state.month = ti.tm_mon + 1;
    state.wday = ti.tm_wday;
    state.station_name = station.name;
    state.forecast = &grid;

    // BLE defaults (until BLE scanning is added)
    state.solar_w = NAN;
    state.charger_w = NAN;
    state.battery_w = NAN;
    state.engine_v = NAN;
    state.saloon_temp = NAN;
    state.saloon_humidity = NAN;
    state.icebox_temp = NAN;
    state.timer_active = false;

    if (wifi_ok) {
        clock91_fetch_fmi(station, state, grid);
    }

    clock91_compute_alarm(station, ti, state);

    DrawList dl = buildLayout(state);
    renderFull(dl);

    if (wifi_ok) {
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
    }
}

// ── Partial cycle: update clock digits only, no WiFi ──

static void clock91_partial_cycle(void) {
    struct tm ti;
    time_t now = time(NULL);
    localtime_r(&now, &ti);

    renderClockPartial(ti.tm_hour, ti.tm_min);
}

// ── Gesture handling ──

static bool clock91_handle_gesture(Clock91Gesture gesture) {
    if (gesture == CLOCK91_GESTURE_NONE) return false;

    int station_index = preferences.getUInt("station_idx", 0);
    bool changed = false;

    switch (gesture) {
    case CLOCK91_GESTURE_PREV:
        station_index = (station_index - 1 + STATION_COUNT) % STATION_COUNT;
        preferences.putUInt("station_idx", station_index);
        Log_info("clock91: station prev -> %d (%s)",
                 station_index, STATIONS[station_index].name);
        changed = true;
        break;

    case CLOCK91_GESTURE_NEXT:
        station_index = (station_index + 1) % STATION_COUNT;
        preferences.putUInt("station_idx", station_index);
        Log_info("clock91: station next -> %d (%s)",
                 station_index, STATIONS[station_index].name);
        changed = true;
        break;

    case CLOCK91_GESTURE_TAP_MIDDLE:
        // TODO: timer start/cancel
        Log_info("clock91: middle tap (timer not yet implemented)");
        break;

    default:
        break;
    }

    return changed;
}

// ── Entry point ──

void clock91_cycle(Clock91Gesture gesture) {
    setenv("TZ", TIMEZONE, 1);
    tzset();

    render_init();

    bool station_changed = clock91_handle_gesture(gesture);

    struct tm ti;
    time_t now = time(NULL);
    localtime_r(&now, &ti);

    esp_sleep_wakeup_cause_t wakeup = esp_sleep_get_wakeup_cause();
    bool cold_boot = (wakeup == ESP_SLEEP_WAKEUP_UNDEFINED);
    bool full = cold_boot || station_changed || (ti.tm_min % 15 == 0);

    Log_info("clock91: %02d:%02d %s cycle%s%s",
             ti.tm_hour, ti.tm_min, full ? "FULL" : "partial",
             cold_boot ? " (cold boot)" : "",
             station_changed ? " (station change)" : "");

    if (full) {
        clock91_full_cycle();
    } else {
        clock91_partial_cycle();
    }

    Log_info("clock91: cycle done");
}

#endif // CLOCK91_MODE
