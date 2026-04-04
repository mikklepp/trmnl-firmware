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
#include "timer.h"
#include "IQS323.h"
#include "iqs323_task.h"

extern Preferences preferences;
extern IQS323 iqs323;

// Europe/Helsinki: UTC+2 (winter) / UTC+3 (summer)
static const char* TIMEZONE = "EET-2EEST,M3.5.0/3,M10.5.0/4";

// Previous time for partial refresh (survives deep sleep)
static RTC_DATA_ATTR int rtc_prev_hour = -1;
static RTC_DATA_ATTR int rtc_prev_minute = -1;

// ── Timer touch polling ──

// Volatile flag set by IQS323 data callback from its FreeRTOS task.
static volatile bool touch_pending = false;

static void IRAM_ATTR on_iqs323_data(void) {
    touch_pending = true;
}

// Poll for a gesture. Returns the gesture event or IQS323_GESTURE_NONE.
// Must be called from the main task context (not from the callback).
static iqs323_gesture_events clock91_poll_gesture(void) {
    if (!touch_pending) return IQS323_GESTURE_NONE;
    touch_pending = false;

    iqs323_task_i2c_lock();
    bool has_gesture = iqs323.getSliderEvent();
    iqs323_gesture_events gesture = IQS323_GESTURE_NONE;
    if (has_gesture) {
        gesture = iqs323.getGestureType();
    }
    iqs323_task_i2c_unlock();
    return gesture;
}

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

    // Save time for partial refresh on next wake
    rtc_prev_hour = ti.tm_hour;
    rtc_prev_minute = ti.tm_min;

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

    if (rtc_prev_hour < 0) {
        // No previous time (first boot?) — force full cycle instead
        Log_info("clock91: no previous time, forcing full cycle");
        clock91_full_cycle();
        return;
    }

    renderClockPrepare(rtc_prev_hour, rtc_prev_minute);
    renderClockUpdate(ti.tm_hour, ti.tm_min);

    rtc_prev_hour = ti.tm_hour;
    rtc_prev_minute = ti.tm_min;
}

// ── Gesture handling (normal mode, not timer) ──

enum GestureResult {
    GESTURE_NONE,
    GESTURE_STATION_CHANGED,
    GESTURE_TIMER_START,
};

static GestureResult clock91_handle_gesture(Clock91Gesture gesture) {
    if (gesture == CLOCK91_GESTURE_NONE) return GESTURE_NONE;

    int station_index = preferences.getUInt("station_idx", 0);

    switch (gesture) {
    case CLOCK91_GESTURE_PREV:
        station_index = (station_index - 1 + STATION_COUNT) % STATION_COUNT;
        preferences.putUInt("station_idx", station_index);
        Log_info("clock91: station prev -> %d (%s)",
                 station_index, STATIONS[station_index].name);
        return GESTURE_STATION_CHANGED;

    case CLOCK91_GESTURE_NEXT:
        station_index = (station_index + 1) % STATION_COUNT;
        preferences.putUInt("station_idx", station_index);
        Log_info("clock91: station next -> %d (%s)",
                 station_index, STATIONS[station_index].name);
        return GESTURE_STATION_CHANGED;

    case CLOCK91_GESTURE_TAP_MIDDLE:
        Log_info("clock91: middle tap -> timer start");
        return GESTURE_TIMER_START;

    default:
        return GESTURE_NONE;
    }
}

// ── Timer countdown loop ──
// Stays awake for the entire countdown. Uses millis() anchoring so
// e-ink refresh time doesn't accumulate as drift.

// Build a DisplayState for timer mode and render it.
static void clock91_render_timer_frame(const TimerState& timer,
                                        uint32_t start_ms, int frame) {
    struct tm ti;
    time_t now = time(NULL);
    localtime_r(&now, &ti);

    DisplayState state = {};
    state.hour = ti.tm_hour;
    state.minute = ti.tm_min;
    state.timer_active = true;
    state.timer_seconds = timer.remaining;
    state.timer_total = timer.total;
    state.timer_frame = frame;

    DrawList dl = buildLayout(state);
    renderTimerFrame(dl);
}

static void clock91_timer_loop(void) {
    TimerState timer = {};
    timer.remaining = -1;
    timerStart(&timer);

    // Register touch callback so we can detect gestures while looping
    touch_pending = false;
    iqs323_task_set_data_callback(on_iqs323_data);

    uint32_t start_ms = millis();
    int frame = 0;

    // Initial render (full refresh for clean transition into timer mode)
    {
        DisplayState state = {};
        struct tm ti;
        time_t now_t = time(NULL);
        localtime_r(&now_t, &ti);
        state.hour = ti.tm_hour;
        state.minute = ti.tm_min;
        state.timer_active = true;
        state.timer_seconds = timer.remaining;
        state.timer_total = timer.total;
        state.timer_frame = 0;
        DrawList dl = buildLayout(state);
        renderFull(dl);
    }

    while (timerActive(timer)) {
        // Compute remaining from wall clock (no drift accumulation)
        uint32_t elapsed_ms = millis() - start_ms;
        int elapsed_s = elapsed_ms / 1000;
        int remaining = timer.total - elapsed_s;

        if (remaining <= 0) {
            timer.remaining = -1;
            timer.buzzing = true;
            break;
        }
        timer.remaining = remaining;

        // Full-screen partial refresh: clock, coffee cup steam, timer digits
        clock91_render_timer_frame(timer, start_ms, frame);
        frame = (frame + 1) % 3;

        // Poll for touch gestures
        iqs323_gesture_events gesture = clock91_poll_gesture();
        if (gesture == IQS323_GESTURE_TAP) {
            Log_info("clock91: timer cancelled by tap");
            timerCancel(&timer);
            break;
        } else if (gesture == IQS323_GESTURE_SWIPE_POSITIVE
                   || gesture == IQS323_GESTURE_FLICK_POSITIVE) {
            timerNext(&timer);
            start_ms = millis();
            Log_info("clock91: timer next preset %ds", timer.total);
        } else if (gesture == IQS323_GESTURE_SWIPE_NEGATIVE
                   || gesture == IQS323_GESTURE_FLICK_NEGATIVE) {
            timerPrev(&timer);
            start_ms = millis();
            Log_info("clock91: timer prev preset %ds", timer.total);
        }

        // Sleep until the next whole second boundary
        uint32_t next_s_ms = start_ms + ((uint32_t)(elapsed_s + 1) * 1000);
        uint32_t now_ms = millis();
        if (next_s_ms > now_ms) {
            delay(next_s_ms - now_ms);
        }
    }

    // Unregister callback
    iqs323_task_set_data_callback(NULL);

    if (timer.buzzing) {
        Log_info("clock91: timer done — buzzing");
        // TODO: buzz_timer_pattern() once buzzer driver is built
    }

    // Return to normal display — caller will do a full or partial cycle
}

// ── Entry point ──

void clock91_cycle(Clock91Gesture gesture) {
    setenv("TZ", TIMEZONE, 1);
    tzset();

    render_init();

    GestureResult gr = clock91_handle_gesture(gesture);

    if (gr == GESTURE_TIMER_START) {
        clock91_timer_loop();
        // After timer, do a full cycle to restore the normal display
        clock91_full_cycle();
        Log_info("clock91: cycle done (post-timer)");
        return;
    }

    bool station_changed = (gr == GESTURE_STATION_CHANGED);

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
