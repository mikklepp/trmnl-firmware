#include "clock91.h"

#ifdef CLOCK91_MODE

#include <Arduino.h>
#include <WiFi.h>
#include <WifiCaptive.h>
#include <trmnl_log.h>
#include <bl.h>
#include <config.h>
#include "layout.h"
#include "render.h"

// Europe/Helsinki: UTC+2 (winter) / UTC+3 (summer)
// ESP-IDF POSIX TZ string for automatic DST handling
static const char* TIMEZONE = "EET-2EEST,M3.5.0/3,M10.5.0/4";

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
    // Set timezone for localtime() calls
    setenv("TZ", TIMEZONE, 1);
    tzset();

    // Check if time is already roughly valid (post-2024 epoch)
    time_t now = time(NULL);
    if (now > 1704067200) {  // 2024-01-01 00:00:00 UTC
        struct tm ti;
        localtime_r(&now, &ti);
        Log_info("clock91: time already set: %02d:%02d (day %d)",
                 ti.tm_hour, ti.tm_min, ti.tm_mday);
        // Still sync if it's been a while, but don't block on it
    }

    // NTP sync (non-blocking after first call; ESP-IDF syncs in background)
    configTime(0, 0, "time.google.com", "time.cloudflare.com");

    // Wait up to 5 seconds for sync
    struct tm timeinfo = {};
    for (int i = 0; i < 50; i++) {
        if (getLocalTime(&timeinfo, 100)) {
            Log_info("clock91: NTP synced: %04d-%02d-%02d %02d:%02d:%02d",
                     timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                     timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
            return true;
        }
    }

    Log_error("clock91: NTP sync failed, using RTC time");
    return (now > 1704067200);  // true if RTC has plausible time
}

static DisplayState clock91_build_state(void) {
    DisplayState s = {};

    // Time
    struct tm ti;
    time_t now = time(NULL);
    localtime_r(&now, &ti);

    s.hour = ti.tm_hour;
    s.minute = ti.tm_min;
    s.day = ti.tm_mday;
    s.month = ti.tm_mon + 1;
    s.wday = ti.tm_wday;  // 0=Sun

    // Everything else defaults to zero/NAN — will show as dashes
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

void clock91_cycle(void) {
    Log_info("clock91: wake cycle start");

    render_init();

    bool wifi_ok = clock91_wifi_connect();

    if (wifi_ok) {
        clock91_sync_time();
    }

    // Build state and render even without WiFi — RTC may have valid time
    DisplayState state = clock91_build_state();
    DrawList dl = buildLayout(state);

    if (state.hour == 0 && state.minute == 0 && !wifi_ok) {
        // No time at all — show dashes
        Log_error("clock91: no valid time, rendering blank");
    }

    renderFull(dl);

    // Disconnect WiFi to save power before sleep
    if (wifi_ok) {
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
    }

    Log_info("clock91: wake cycle done");
}

#endif // CLOCK91_MODE
