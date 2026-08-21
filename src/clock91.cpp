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
#include <BQ27427.h>   // fuel gauge, for the device battery reading
#include "buzzer.h"
#include "ble_scan.h"
#include "esp_sntp.h"
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

// Pick which of the three touchbar zones a hold belongs to.
//
// The zones sit on one continuous capacitive bar and overlap: a fingertip is
// wider than the gap between them, so a hold over [ SETUP ] (the middle zone)
// also registers a touch on CH0. The previous version tested the channels in
// order and returned on the first hit, so every centre hold came back as
// HOLD_LEFT — which is hibernate, the most disruptive action on the bar, and it
// made the captive portal unreachable by gesture.
//
// Compare deflection from each channel's long-term average instead. Raw counts
// are not comparable across channels (different baselines and sensitivities);
// LTA - counts normalises that out, and the largest deflection is the zone the
// finger is actually centred over.
// A deliberate press is one channel clearly ahead of the others; gripping the
// case couples into several at once. Require the winner to lead the runner-up
// by this factor (in eighths, so 12 = 1.5x) before acting on it.
//
// Observed while the case was being held: ch0 521, ch2 137 — both far past the
// 26-27 count touch threshold, so no threshold that still admits a fingertip
// could have rejected that. The ratio does: 521 vs 137 is 3.8x and passes,
// whereas the near-equal deflections a two-handed grip produces do not.
#define HOLD_DOMINANCE_EIGHTHS 12

static Gesture hold_channel_gesture(IQS323& iqs) {
    static const iqs323_channel_e CH[3] = {IQS323_CH0, IQS323_CH1, IQS323_CH2};
    static const Gesture MAP[3] = {GESTURE_HOLD_LEFT, GESTURE_HOLD_MID, GESTURE_HOLD_RIGHT};

    int32_t best_defl = 0;
    int best = -1;
    int32_t defl[3] = {0, 0, 0};
    bool touched[3] = {false, false, false};

    for (int i = 0; i < 3; i++) {
        touched[i] = iqs.channel_touchState(CH[i]);
        // Counts fall below the LTA as capacitance rises, so deflection is
        // LTA - counts. Clamp at 0: a channel reading above its baseline is
        // noise, not a touch.
        int32_t lta = (int32_t)iqs.readChannelLTA(CH[i]);
        int32_t cnt = (int32_t)iqs.readChannelCounts(CH[i]);
        defl[i] = lta - cnt;
        if (defl[i] < 0) defl[i] = 0;

        if (touched[i] && defl[i] > best_defl) {
            best_defl = defl[i];
            best = i;
        }
    }

    // Second-largest deflection, whether or not that channel latched a touch —
    // a grip often lights one channel and merely loads the next.
    int32_t runner_up = 0;
    for (int i = 0; i < 3; i++) {
        if (i != best && defl[i] > runner_up) runner_up = defl[i];
    }
    bool dominant = (best >= 0) &&
                    (best_defl * 8 >= runner_up * HOLD_DOMINANCE_EIGHTHS);

    Log_info("clock91: hold ch0=%s/%ld ch1=%s/%ld ch2=%s/%ld -> %s%s",
             touched[0] ? "T" : "-", (long)defl[0],
             touched[1] ? "T" : "-", (long)defl[1],
             touched[2] ? "T" : "-", (long)defl[2],
             best < 0 ? "none" : (best == 0 ? "LEFT" : (best == 1 ? "MID" : "RIGHT")),
             (best >= 0 && !dominant) ? " (rejected: no dominant channel)" : "");

    if (dominant) return MAP[best];

    // No channel reported a touch (the hold ended before we read the states).
    // Do nothing rather than guessing — the old fallback here was HOLD_MID,
    // which silently launched the captive portal.
    return GESTURE_NONE;
}

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
        return hold_channel_gesture(iqs);
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
    // Debug builds busy-wait instead of sleeping so the USB serial link stays
    // up. Break out as soon as the touch callback fires, otherwise a gesture
    // would sit unnoticed until the next minute boundary and look like it was
    // ignored. Production (light sleep) gets the same responsiveness from the
    // GPIO wake below.
    for (uint32_t i = 0; i < seconds * 10 && !touch_pending; i++) {
        delay(100);
    }
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

// Drop the WiFi connection so BLE can use the radio, without tearing the
// driver down.
//
// Any route into esp_wifi_deinit() hangs on this build
// (CONFIG_ESP_COEX_SW_COEXIST_ENABLE=y with CONFIG_BT_ENABLED=y): the task
// stops making progress and dies to the watchdog, with no panic to point at
// it. Both WiFi.mode(WIFI_OFF) and WiFi.disconnect(true) reach it.
//
// Unreachable until WiFi credentials were saved — clock91_wifi_connect() bails
// out early without them — which is why this survived every earlier test on
// this hardware and only appeared after the captive portal succeeded.
//
// Leaving the driver initialised costs some idle current versus a full deinit.
// If that ever matters, the deinit belongs once before sleep, not on every
// cycle — and it will need solving for coexistence either way.
static void clock91_wifi_off(void) {
    if (WiFi.getMode() == WIFI_MODE_NULL) return;

    // disconnect(false), NOT disconnect(true). The bool is `wifioff`, and
    // disconnect(true) calls STA.end() → WiFi.enableSTA(false) → espWiFiStop()
    // → esp_wifi_deinit(): a full driver teardown. On this build
    // (CONFIG_ESP_COEX_SW_COEXIST_ENABLE=y, CONFIG_BT_ENABLED=y) that never
    // returns, and the task dies to the watchdog — "PRO CPU has been reset by
    // WDT" with no panic and no backtrace. Traced by bracketing the call: the
    // log line after it never printed, while stack headroom was a comfortable
    // 6284 bytes, ruling out overflow.
    //
    // disconnect(false) drops the association and leaves the driver up, which
    // is all BLE needs — WiFi/BLE coexistence shares the radio by design.
    Log_info("TRACE: wifi_off pre-disconnect mode=%d status=%d",
             (int)WiFi.getMode(), (int)WiFi.status());
    WiFi.disconnect(false);
    Log_info("TRACE: wifi_off post-disconnect");

    unsigned long deadline = millis() + 1000;
    while (WiFi.status() == WL_CONNECTED && millis() < deadline) {
        delay(10);
    }
    Log_info("clock91: WiFi disconnected (mode=%d, driver left up for coex)",
             (int)WiFi.getMode());
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

// Fill *out with the current local time.
static void localtime_r_now(struct tm *out) {
    time_t now = time(NULL);
    localtime_r(&now, out);
}

static bool clock91_sync_time(void) {
    // Clear the sync status first, so the wait below cannot observe a COMPLETED
    // left over from an earlier sync in this boot.
    sntp_set_sync_status(SNTP_SYNC_STATUS_RESET);

    // configTzTime, not configTime(0, 0, ...): the latter sets a UTC offset and
    // overwrites the TZ environment variable clock91_init() installed, so every
    // sync reverted the display to UTC — three hours behind Helsinki in summer.
    configTzTime(TIMEZONE, "time.google.com", "time.cloudflare.com");

    // Wait for SNTP to actually land a packet, not merely for the clock to look
    // plausible. getLocalTime() returns true as soon as the system time is past
    // 2016, which the RTC already satisfies across a deep-sleep wake — so
    // polling it accepted the pre-sync RTC value on the first iteration and
    // returned "synced" without ever applying the NTP reply. That left the
    // display running at whatever offset the RTC had drifted to.
    struct tm timeinfo = {};
    for (int i = 0; i < 100; i++) { // up to ~10 s
        if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
            localtime_r_now(&timeinfo);
            Log_info("clock91: NTP synced: %04d-%02d-%02d %02d:%02d:%02d",
                     timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                     timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
            return true;
        }
        delay(100);
    }

    Log_error("clock91: NTP sync failed (no SNTP reply in 10 s)");
    return false;
}

// ── FMI fetch ──

// Static rather than locals: FmiForecastArrays is ~872 bytes and sea_hours
// another 288, and this runs deep in a chain that already holds HTTPClient,
// TLS buffers and a multi-megabyte response String. Keeping them off the
// Arduino loop task's stack is what stops the watchdog reset — see the note on
// parseFmiResponseInto() in fmi_parse.h. Single-task and sequential, so one
// copy of each is fine.
static FmiForecastArrays g_fc;
static int g_sea_hours[FMI_MAX_TIMESTEPS];

// The two forecast days and the history strip the screen renders. Static for
// the same reason as g_fc — DayForecast is ~700 bytes each.
static DayForecast g_days[FORECAST_DAYS];
static WindHistory g_history;

// ~14 KB. Static for the same reason as g_fc and g_parsed: the loop task's
// 16 KB stack cannot hold it alongside WiFi, TLS and HTTPClient. See the note
// on buildLayoutInto() in layout.h.
static DrawList g_dl;

static void clock91_fetch_fmi(const FmiStation& station, DisplayState& s,
                               DayForecast* days, WindHistory* hist) {
    // Decided up front so it holds even if the forecast fetch fails: the strips
    // would then be empty, but nothing should be greyed as elapsed either.
    {
        struct tm ti_now;
        time_t t = time(NULL);
        localtime_r(&t, &ti_now);
        s.forecast_rolled = (flagDownOffset(ti_now.tm_hour) > 0);
        // Refined below once the series' own start day is known.
    }

    FmiObservations obs = fmiFetchObservations(station);
    if (obs.valid) {
        s.wind_speed = obs.wind_speed;
        s.air_temp = obs.temperature;
        s.wind_gust = obs.wind_gust;
        s.wind_dir = obs.wind_dir;
    }

    g_fc = fmiFetchWindForecast(station);
    if (g_fc.valid) {
        struct tm ti;
        time_t now = time(NULL);
        localtime_r(&now, &ti);

        // Which day the first strip shows, as an index into the series.
        //
        // flagDownOffset() says which calendar day we want to start from (today,
        // or tomorrow once the evening cutoff has passed). The series may already
        // begin on a later day than today — it usually starts at the next whole
        // hour, which after 23:00 is tomorrow — so subtract what it has already
        // skipped. Without this the offset was counted twice and the second
        // strip ran off the end of the data, leaving it with a single bar.
        int want = flagDownOffset(ti.tm_hour);
        int base = want - g_fc.start_day_offset;
        if (base < 0) base = 0;
        for (int d = 0; d < FORECAST_DAYS; d++) {
            fillDayForecast(&days[d], g_fc.wind, g_fc.gust, g_fc.rain, g_fc.dir,
                            g_fc.count, g_fc.start_hour, base + d);
            // The weekday of the strip, counted from today plus whatever the
            // series already skipped plus the strip's own index.
            days[d].wday = (ti.tm_wday + g_fc.start_day_offset + base + d) % 7;
        }

        int sea_count = fmiFetchSeaLevel(station, g_sea_hours, FMI_MAX_TIMESTEPS);
        for (int d = 0; d < FORECAST_DAYS; d++) {
            if (sea_count > 0) {
                fillDaySeaLevel(&days[d], g_sea_hours, sea_count, g_fc.start_hour, base + d);
            }
            computeDaySummary(&days[d], g_fc.air, g_fc.count, g_fc.start_hour, base + d);
        }

    }

    // -2h -> NOW strip: its own query, since it needs 15-minute steps over a
    // bounded window rather than the hourly forecast grid.
    if (hist) {
        FmiObsHistory oh = fmiFetchObsHistory(station, time(NULL));
        if (oh.valid) {
            struct tm ti;
            time_t now = time(NULL);
            localtime_r(&now, &ti);
            fillWindHistory(hist, oh.wind, oh.gust, oh.dir, oh.count, ti.tm_hour);
        } else {
            hist->count = 0;
        }
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
    for (int d = 0; d < FORECAST_DAYS; d++) g_days[d] = DayForecast{};
    g_history = WindHistory{};

    state.hour = ti.tm_hour;
    state.minute = ti.tm_min;
    state.day = ti.tm_mday;
    state.month = ti.tm_mon + 1;
    state.wday = ti.tm_wday;
    state.station_name = station.name;
    state.days = g_days;
    state.history = &g_history;

    state.timer_active = false;
    state.otg_enabled = otg_enabled;

    if (wifi_ok) {
        Log_info("TRACE: fetch_fmi enter (stack free=%u)",
                 (unsigned)uxTaskGetStackHighWaterMark(NULL));
        clock91_fetch_fmi(station, state, g_days, &g_history);
        Log_info("TRACE: fetch_fmi done (stack free=%u)",
                 (unsigned)uxTaskGetStackHighWaterMark(NULL));
        clock91_wifi_off();
        Log_info("TRACE: wifi_off done");
    }

    // BLE scan (runs after WiFi is off — they share the radio)
    Log_info("TRACE: ble_scan enter");
    BleScanResult ble = ble_scan_run(10);
    Log_info("TRACE: ble_scan done");

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
    // TRMNL's own battery. getLipoSOC() is upstream's reading: with
    // BYPASS_BQ27427_SOC (set by both TRMNL X envs) it derives the percentage
    // from the gauge's voltage instead of the gauge's own SOC, which reads
    // garbage until the IT algorithm leaves INITIALIZATION. Still -1 if the
    // gauge never came up.
    state.device_pct = lipo._initialized ? getLipoSOC() : -1;
    state.engine_v = ble.shunt.valid ? ble.shunt.aux_voltage : NAN;
    state.saloon_temp = ble.ruuvi_saloon.valid ? ble.ruuvi_saloon.temperature : NAN;
    state.saloon_humidity = ble.ruuvi_saloon.valid ? ble.ruuvi_saloon.humidity : NAN;
    state.icebox_temp = ble.ruuvi_icebox.valid ? ble.ruuvi_icebox.temperature : NAN;

    clock91_compute_alarm(station, ti, state);

    buildLayoutInto(g_dl, state);
    DrawList& dl = g_dl;
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
    // Station-name field fits ~12 chars before running into the next zone, so
    // the old "SETUP  WiFi: TRMNL" was being clipped mid-SSID. The AP name is
    // TRMNL-<mac suffix> and cannot fit here at all; showing the full SSID and
    // the hold-to-exit hint needs a setup-specific layout rather than this
    // borrowed one.
    state.station_name = "SETUP  MODE";
    state.solar_w = NAN;
    state.ac_w = NAN;
    state.house_w = NAN;
    state.engine_v = NAN;
    state.saloon_temp = NAN;
    state.saloon_humidity = NAN;
    state.icebox_temp = NAN;
    state.timer_active = false;
    state.days = NULL;
    state.history = NULL;

    buildLayoutInto(g_dl, state);
    DrawList& dl = g_dl;
    renderFull(dl);
}

// Guard against relaunching the portal on top of itself. startPortal() begins
// by stopping and re-initialising the WiFi driver (esp_wifi_deinit(), an AMPDU
// workaround in WifiCaptive.cpp); running that while the previous run's
// teardown is still settling crashes inside esp_wifi_deinit with
// LoadProhibited, and the device then reboots into progressively worse state.
//
// Seen on hardware: a successful portal run finished at t=160.6, a second hold
// fired 9s later, and the device panicked and rebooted three times. A held
// finger easily produces two HOLD events.
static uint32_t last_portal_exit_ms = 0;
static const uint32_t PORTAL_REENTRY_GUARD_MS = 30000;

static void clock91_start_portal(void) {
    if (last_portal_exit_ms != 0 &&
        (millis() - last_portal_exit_ms) < PORTAL_REENTRY_GUARD_MS) {
        Log_info("clock91: portal re-entry ignored (%lu ms since last exit)",
                 (unsigned long)(millis() - last_portal_exit_ms));
        return;
    }

    Log_info("clock91: starting captive portal (hold gesture)");
    buzzer_beep();
    clock91_render_setup_screen();

    // Let a hold gesture cancel the portal. Without this, startPortal() loops
    // until credentials are submitted over WiFi (while(1) in WifiCaptive.cpp)
    // — so an accidental SETUP hold takes the display out until someone can
    // reach it with a phone. Requiring a hold rather than any touch keeps a
    // stray tap from cancelling a setup in progress.
    //
    // Ignores touches for the first few seconds: the hold that opened the
    // portal is often still in progress here and would cancel it immediately.
    {
        uint32_t portal_start = millis();
        WifiCaptivePortal.setAbortCallback([portal_start]() -> bool {
            if (millis() - portal_start < 3000) {
                touch_pending = false;
                return false;
            }
            if (!touch_pending) return false;
            touch_pending = false;

            iqs323_task_i2c_lock();
            bool has_gesture = iqs323.getSliderEvent();
            iqs323_gesture_events ev =
                has_gesture ? iqs323.getGestureType() : IQS323_GESTURE_NONE;
            iqs323_task_i2c_unlock();

            if (ev == IQS323_GESTURE_HOLD) {
                Log_info("clock91: portal cancelled by hold gesture");
                return true;
            }
            return false;
        });
    }

    // Portal blocks until WiFi is configured or the abort callback fires
    WifiCaptivePortal.startPortal();
    WifiCaptivePortal.setAbortCallback(nullptr);

    if (WiFi.status() == WL_CONNECTED) {
        Log_info("clock91: portal done, WiFi connected");
        clock91_wifi_off();
    } else {
        Log_info("clock91: portal done, no WiFi");
    }

    ble_config_reload();

    // Drop any gesture that arrived while the portal was up — otherwise the
    // hold that opened it is still queued and immediately reopens it.
    last_portal_exit_ms = millis();
    touch_pending = false;
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
    state.days = NULL;
    state.history = NULL;
    buildLayoutInto(g_dl, state);
    DrawList& dl = g_dl;
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

// Writes into g_dl rather than returning a DrawList: it is ~14 KB and this runs
// on the 16 KB loop task. See buildLayoutInto() in layout.h.
static void clock91_build_timer_layout(const TimerState& timer) {
    struct tm ti;
    time_t now = time(NULL);
    localtime_r(&now, &ti);

    DisplayState state = {};
    state.hour = ti.tm_hour;
    state.minute = ti.tm_min;
    // The timer screen keeps the clock header, so it needs the date fields too.
    // Leaving them zeroed rendered "00-00" and SUN on every timer screen.
    state.day = ti.tm_mday;
    state.month = ti.tm_mon + 1;
    state.wday = ti.tm_wday;
    state.timer_active = true;
    state.timer_seconds = timer.remaining;
    state.timer_total = timer.total;

    buildLayoutInto(g_dl, state);
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
        clock91_build_timer_layout(timer);
        DrawList& dl = g_dl;
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
            clock91_build_timer_layout(timer);
            DrawList& dl = g_dl;
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

    Log_info("clock91: sleeping %lu s", (unsigned long)sleep_secs);
    clock91_sleep(sleep_secs);

    // Determine wake cause
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    bool touch_wake = (cause == ESP_SLEEP_WAKEUP_GPIO);

    // Poll on every wake, not just GPIO wakes. The touch callback sets
    // touch_pending whenever the IQS323 signals, but gating the poll on
    // touch_wake threw those away on a timer wake — and on DO_NOT_LIGHT_SLEEP
    // builds clock91_sleep() is a plain delay(), so the cause is never
    // ESP_SLEEP_WAKEUP_GPIO and no gesture could ever be seen. Polling
    // unconditionally also picks up gestures made while the device was awake.
    Gesture gesture = GESTURE_NONE;
    if (touch_pending) {
        gesture = clock91_poll_gesture();
        Log_info("clock91: gesture=%d (%s wake)", gesture,
                 touch_wake ? "touch" : "timer");
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
