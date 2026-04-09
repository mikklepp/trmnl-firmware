#include "clock91.h"

#ifdef CLOCK91_MODE

#include <Arduino.h>
#include <WiFi.h>
#include <WifiCaptive.h>
#include <Preferences.h>
#include <esp_sleep.h>
#include <esp_heap_caps.h>
#include <driver/gpio.h>
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
#include "buzzer.h"
#include "ble_scan.h"
#include "display.h"
#include "IQS323.h"
#include "iqs323_task.h"

extern Preferences preferences;
extern IQS323 iqs323;

// Europe/Helsinki: UTC+2 (winter) / UTC+3 (summer)
static const char* TIMEZONE = "EET-2EEST,M3.5.0/3,M10.5.0/4";

// Previous time for partial refresh (survives light sleep — regular static)
static int prev_hour = -1;
static int prev_minute = -1;

// Deep sleep reboot: reclaim heap once per day
static const int REBOOT_HOUR = 3;
static const int REBOOT_MINUTE = 0;
static const size_t HEAP_MIN_THRESHOLD = 32768;  // 32 KB

// USB OTG state
static bool otg_enabled = false;

// ── Touch polling ──

static volatile bool touch_pending = false;

static void IRAM_ATTR on_iqs323_data(void) {
    touch_pending = true;
}

// ── Gesture map ──
//
// Normal mode:
//   Swipe/flick left   → PREV       → previous FMI station
//   Swipe/flick right  → NEXT       → next FMI station
//   Tap                → TAP        → start timer
//   Hold left  (CH0)   → HOLD_LEFT  → hibernate (display off)
//   Hold middle (CH1)  → HOLD_MID   → captive portal (WiFi + BLE config)
//   Hold right (CH2)   → HOLD_RIGHT → toggle USB OTG
//
// Timer mode (raw IQS323 events, not translated):
//   Tap               → cancel timer
//   Swipe/flick right → next timer preset, restart
//   Swipe/flick left  → previous timer preset, restart
//
// Status bar at bottom of screen shows:
//   [OFF]          [SETUP]          [USB: ON/OFF]

enum Gesture {
    GESTURE_NONE,
    GESTURE_PREV,
    GESTURE_NEXT,
    GESTURE_TAP,
    GESTURE_HOLD_LEFT,
    GESTURE_HOLD_MID,
    GESTURE_HOLD_RIGHT,
};

static Gesture translate_gesture(iqs323_gesture_events ev, IQS323& iqs) {
    switch (ev) {
    case IQS323_GESTURE_SWIPE_NEGATIVE:
    case IQS323_GESTURE_FLICK_NEGATIVE:
        return GESTURE_PREV;
    case IQS323_GESTURE_SWIPE_POSITIVE:
    case IQS323_GESTURE_FLICK_POSITIVE:
        return GESTURE_NEXT;
    case IQS323_GESTURE_TAP:
        return GESTURE_TAP;
    case IQS323_GESTURE_HOLD:
        // Determine which channel is held
        if (iqs.channel_touchState(IQS323_CH0)) return GESTURE_HOLD_LEFT;
        if (iqs.channel_touchState(IQS323_CH1)) return GESTURE_HOLD_MID;
        if (iqs.channel_touchState(IQS323_CH2)) return GESTURE_HOLD_RIGHT;
        return GESTURE_HOLD_MID;  // fallback
    default:
        return GESTURE_NONE;
    }
}

static Gesture clock91_poll_gesture(void) {
    if (!touch_pending) return GESTURE_NONE;
    touch_pending = false;

    iqs323_task_i2c_lock();
    Gesture result = GESTURE_NONE;
    bool has_gesture = iqs323.getSliderEvent();
    if (has_gesture) {
        iqs323_gesture_events ev = iqs323.getGestureType();
        result = translate_gesture(ev, iqs323);
    }
    iqs323_task_i2c_unlock();
    return result;
}

// ── Light sleep ──

static void clock91_sleep(uint32_t seconds) {
#ifdef DO_NOT_LIGHT_SLEEP
    delay(seconds * 1000);
#else
    // Lock I2C to prevent IQS323 task from being frozen mid-transaction
    iqs323_task_i2c_lock();

    // Set event mode: IQS323 only fires RDY on touch events (not streaming)
    iqs323.setEventMode(STOP);

    // Configure wake sources
    esp_sleep_enable_timer_wakeup((uint64_t)seconds * 1000000ULL);
    gpio_wakeup_enable((gpio_num_t)PIN_INTERRUPT, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();

    esp_light_sleep_start();

    // Back to streaming mode
    iqs323.clearEventMode(STOP);
    iqs323_task_i2c_unlock();
#endif
}

// ── WiFi ──

static void wait_wifi_off(uint32_t timeout_ms = 500) {
    unsigned long deadline = millis() + timeout_ms;
    while (WiFi.getMode() != WIFI_MODE_NULL && millis() < deadline) {
        delay(10);
    }
    if (WiFi.getMode() != WIFI_MODE_NULL) {
        Log_error("clock91: WiFi did not turn off within %lu ms", (unsigned long)timeout_ms);
    }
}

static bool clock91_wifi_connect(void) {
    if (!WifiCaptivePortal.isSaved()) {
        Log_info("clock91: no WiFi credentials (use hold gesture to configure)");
        return false;
    }

    WiFi.mode(WIFI_STA);
    int res = WifiCaptivePortal.autoConnect();
    if (!res) {
        Log_error("clock91: WiFi connect failed (status=%d)", WiFi.status());
        WiFi.mode(WIFI_OFF);
        return false;
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

    state.timer_active = false;
    state.otg_enabled = otg_enabled;

    if (wifi_ok) {
        clock91_fetch_fmi(station, state, grid);
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        wait_wifi_off();
    }

    // BLE scan (runs after WiFi is off — they share the radio)
    BleScanResult ble = ble_scan_run(10);

    // Solar PV input
    state.solar_w = ble.solar.valid ? ble.solar.pv_power : NAN;

    // AC: VE.Bus battery power (+ = shore charging, - = inverting)
    float vebus_w = ble.vebus.valid ? (ble.vebus.battery_voltage * ble.vebus.battery_current) : NAN;
    state.ac_w = vebus_w;

    // Shunt total battery power
    float shunt_w = ble.shunt.valid ? (ble.shunt.battery_voltage * ble.shunt.battery_current) : NAN;

    // Solar's contribution to battery (charger output, not PV input)
    float solar_batt_w = ble.solar.valid ? (ble.solar.battery_voltage * ble.solar.battery_current) : NAN;

    // House = Shunt - Solar_battery - VEBus
    // Negative = house consuming, positive = other source charging (alternator, etc.)
    if (!isnan(shunt_w)) {
        float known = 0;
        if (!isnan(solar_batt_w)) known += solar_batt_w;
        if (!isnan(vebus_w)) known += vebus_w;
        state.house_w = shunt_w - known;
    } else {
        state.house_w = NAN;
    }

    state.battery_pct = ble.shunt.valid && !isnan(ble.shunt.soc) ? (int)ble.shunt.soc : -1;
    state.engine_v = ble.shunt.valid ? ble.shunt.aux_voltage : NAN;
    state.saloon_temp = ble.ruuvi_saloon.valid ? ble.ruuvi_saloon.temperature : NAN;
    state.saloon_humidity = ble.ruuvi_saloon.valid ? ble.ruuvi_saloon.humidity : NAN;
    state.icebox_temp = ble.ruuvi_icebox.valid ? ble.ruuvi_icebox.temperature : NAN;

    clock91_compute_alarm(station, ti, state);

    DrawList dl = buildLayout(state);
    renderFull(dl);

    prev_hour = ti.tm_hour;
    prev_minute = ti.tm_min;
}

// ── Partial cycle: update clock digits only, no WiFi ──

static void clock91_partial_cycle(void) {
    struct tm ti;
    time_t now = time(NULL);
    localtime_r(&now, &ti);

    if (prev_hour < 0) {
        Log_info("clock91: no previous time, forcing full cycle");
        clock91_full_cycle();
        return;
    }

    // Framebuffer pPrevious survives light sleep — just update
    renderClockUpdate(ti.tm_hour, ti.tm_min);

    prev_hour = ti.tm_hour;
    prev_minute = ti.tm_min;
}

// ── Gesture handling ──

// ── Captive portal ──

static void clock91_render_setup_screen(void) {
    // Show setup indicator on screen while portal is active.
    // Use current time if available, dashes for data fields.
    struct tm ti;
    time_t now = time(NULL);
    localtime_r(&now, &ti);

    DisplayState state = {};
    state.hour = ti.tm_hour;
    state.minute = ti.tm_min;
    state.day = ti.tm_mday;
    state.month = ti.tm_mon + 1;
    state.wday = ti.tm_wday;
    state.station_name = "SETUP  WiFi: TRMNL";
    state.solar_w = NAN;
    state.ac_w = NAN;
    state.house_w = NAN;
    state.engine_v = NAN;
    state.saloon_temp = NAN;
    state.saloon_humidity = NAN;
    state.icebox_temp = NAN;
    state.timer_active = false;
    state.forecast = NULL;

    DrawList dl = buildLayout(state);
    renderFull(dl);
}

static void clock91_start_portal(void) {
    Log_info("clock91: starting captive portal (hold gesture)");
    buzzer_beep();
    clock91_render_setup_screen();

    // Portal blocks until WiFi is configured or user cancels
    WifiCaptivePortal.startPortal();

    if (WiFi.status() == WL_CONNECTED) {
        Log_info("clock91: portal done, WiFi connected");
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        wait_wifi_off();
    } else {
        Log_info("clock91: portal done, no WiFi");
    }

    ble_config_reload();
}

// ── Gesture handling ──

// ── OTG toggle ──

static void clock91_toggle_otg(void) {
    if (otg_enabled) {
        otg_turn_off();
        otg_enabled = false;
        Log_info("clock91: OTG off");
    } else {
        otg_turn_on();
        otg_enabled = true;
        Log_info("clock91: OTG on");
    }
    buzzer_beep();
}

// ── Hibernate ──

static void clock91_hibernate(void) {
    Log_info("clock91: entering hibernate (hold left)");
    buzzer_beep();

    // Show a blank screen with just "OFF" indication
    DisplayState state = {};
    state.station_name = "OFF";
    state.solar_w = NAN;
    state.ac_w = NAN;
    state.house_w = NAN;
    state.engine_v = NAN;
    state.saloon_temp = NAN;
    state.saloon_humidity = NAN;
    state.icebox_temp = NAN;
    state.timer_active = false;
    state.forecast = NULL;
    DrawList dl = buildLayout(state);
    renderFull(dl);

    iqs323_task_set_data_callback(NULL);
    bl_hibernate();  // deep sleep, touch wake only, never returns
}

// ── Gesture handling ──

enum GestureResult {
    GR_NONE,
    GR_STATION_CHANGED,
    GR_TIMER_START,
    GR_PORTAL,
    GR_OTG_TOGGLE,
    GR_HIBERNATE,
};

static GestureResult clock91_handle_gesture(Gesture gesture) {
    if (gesture == GESTURE_NONE) return GR_NONE;

    int station_index = preferences.getUInt("station_idx", 0);

    switch (gesture) {
    case GESTURE_PREV:
        station_index = (station_index - 1 + STATION_COUNT) % STATION_COUNT;
        preferences.putUInt("station_idx", station_index);
        Log_info("clock91: station prev -> %d (%s)",
                 station_index, STATIONS[station_index].name);
        return GR_STATION_CHANGED;

    case GESTURE_NEXT:
        station_index = (station_index + 1) % STATION_COUNT;
        preferences.putUInt("station_idx", station_index);
        Log_info("clock91: station next -> %d (%s)",
                 station_index, STATIONS[station_index].name);
        return GR_STATION_CHANGED;

    case GESTURE_TAP:
        Log_info("clock91: tap -> timer start");
        return GR_TIMER_START;

    case GESTURE_HOLD_LEFT:
        Log_info("clock91: hold left -> hibernate");
        return GR_HIBERNATE;

    case GESTURE_HOLD_MID:
        Log_info("clock91: hold middle -> captive portal");
        return GR_PORTAL;

    case GESTURE_HOLD_RIGHT:
        Log_info("clock91: hold right -> OTG toggle");
        return GR_OTG_TOGGLE;

    default:
        return GR_NONE;
    }
}

// ── Timer countdown loop ──

static DrawList clock91_build_timer_layout(const TimerState& timer) {
    struct tm ti;
    time_t now = time(NULL);
    localtime_r(&now, &ti);

    DisplayState state = {};
    state.hour = ti.tm_hour;
    state.minute = ti.tm_min;
    state.timer_active = true;
    state.timer_seconds = timer.remaining;
    state.timer_total = timer.total;

    return buildLayout(state);
}

static void clock91_timer_loop(void) {
    TimerState timer = {};
    timer.remaining = -1;
    timerStart(&timer);

    touch_pending = false;

    uint32_t start_ms = millis();
    int frame = 0;

    // Initial full refresh for clean transition
    {
        DrawList dl = clock91_build_timer_layout(timer);
        renderTimerFull(dl, 0);
    }

    while (timerActive(timer)) {
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
        {
            DrawList dl = clock91_build_timer_layout(timer);
            renderTimerFrame(dl, frame);
        }
        frame = (frame + 1) % 3;

        // Poll for touch gestures
        Gesture gesture = clock91_poll_gesture();
        if (gesture == GESTURE_TAP) {
            Log_info("clock91: timer cancelled by tap");
            timerCancel(&timer);
            break;
        } else if (gesture == GESTURE_NEXT) {
            timerNext(&timer);
            start_ms = millis();
            Log_info("clock91: timer next preset %ds", timer.total);
        } else if (gesture == GESTURE_PREV) {
            timerPrev(&timer);
            start_ms = millis();
            Log_info("clock91: timer prev preset %ds", timer.total);
        }

        // Sleep until the next whole second boundary
        uint32_t next_s_ms = start_ms + ((uint32_t)(elapsed_s + 1) * 1000);
        uint32_t now_ms = millis();
        if (next_s_ms > now_ms) {
            uint32_t wait_ms = next_s_ms - now_ms;
            clock91_sleep(wait_ms / 1000 > 0 ? wait_ms / 1000 : 1);
        }
    }

    if (timer.buzzing) {
        Log_info("clock91: timer done — buzzing");
        buzzer_timer();
    }
}

// ── Deep sleep reboot (heap reclaim) ──

static bool clock91_should_reboot(const struct tm& ti) {
    size_t free_heap = esp_get_free_heap_size();
    size_t min_heap = esp_get_minimum_free_heap_size();

    Log_info("clock91: heap free=%u min_ever=%u", free_heap, min_heap);

    if (free_heap < HEAP_MIN_THRESHOLD) {
        Log_info("clock91: heap below %u — forcing deep sleep reboot", HEAP_MIN_THRESHOLD);
        return true;
    }

    if (ti.tm_hour == REBOOT_HOUR && ti.tm_min == REBOOT_MINUTE) {
        Log_info("clock91: scheduled %02d:%02d reboot", REBOOT_HOUR, REBOOT_MINUTE);
        return true;
    }

    return false;
}

static void clock91_deep_reboot(void) {
    Log_info("clock91: deep sleep reboot — reclaiming heap");
    iqs323_task_set_data_callback(NULL);
    bl_deep_sleep();  // timer wake in ~60s, device restarts with clean heap
}

// ── Init + Loop ──

void clock91_init(void) {
    setenv("TZ", TIMEZONE, 1);
    tzset();

    render_init();
    buzzer_init();
    ble_config_init();

    // Register touch callback (persists across light sleep)
    touch_pending = false;
    iqs323_task_set_data_callback(on_iqs323_data);

    // First cycle is always full
    Log_info("clock91: init — first full cycle");
    clock91_full_cycle();
}

void clock91_loop(void) {
    // Compute sleep duration: align to next whole minute boundary
    time_t now = time(NULL);
    uint32_t sleep_secs = 60 - (now % 60);
    if (sleep_secs < 5) sleep_secs += 60;

    Log_info("clock91: sleeping %u s", sleep_secs);
    clock91_sleep(sleep_secs);

    // Determine wake cause
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    bool touch_wake = (cause == ESP_SLEEP_WAKEUP_GPIO);

    // Check for gesture
    Gesture gesture = GESTURE_NONE;
    if (touch_wake) {
        gesture = clock91_poll_gesture();
        Log_info("clock91: touch wake, gesture=%d", gesture);
    }

    GestureResult gr = clock91_handle_gesture(gesture);

    if (gr == GR_HIBERNATE) {
        clock91_hibernate();  // never returns
    }

    if (gr == GR_TIMER_START) {
        clock91_timer_loop();
        clock91_full_cycle();
        Log_info("clock91: post-timer full cycle done");
        return;
    }

    if (gr == GR_PORTAL) {
        clock91_start_portal();
        clock91_full_cycle();
        Log_info("clock91: post-portal full cycle done");
        return;
    }

    if (gr == GR_OTG_TOGGLE) {
        clock91_toggle_otg();
        clock91_full_cycle();  // refresh to update status bar
        return;
    }

    bool station_changed = (gr == GR_STATION_CHANGED);

    struct tm ti;
    now = time(NULL);
    localtime_r(&now, &ti);

    // Nightly deep sleep reboot to reclaim heap (also triggers on low heap)
    if (!station_changed && clock91_should_reboot(ti)) {
        clock91_deep_reboot();  // never returns
    }

    bool full = station_changed || (ti.tm_min % 15 == 0);

    Log_info("clock91: %02d:%02d %s cycle%s",
             ti.tm_hour, ti.tm_min, full ? "FULL" : "partial",
             station_changed ? " (station change)" : "");

    if (full) {
        clock91_full_cycle();
    } else {
        clock91_partial_cycle();
    }
}

#endif // CLOCK91_MODE
