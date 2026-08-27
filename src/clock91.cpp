#include "clock91.h"

#ifdef CLOCK91_MODE

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
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
#include <esp_wifi.h>
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
//
// Measured: real centre presses separate by only ~1.2x (ch0 389 / ch1 503), not the
// 1.5x this originally demanded, so every SETUP hold was rejected. CH1 has a neighbour
// on both sides and can never separate as cleanly as CH0/CH2. 1.125x clears real presses
// and still rejects the 3.8x grip.
#define HOLD_DOMINANCE_EIGHTHS 9

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

// Raw IQS323 event name, for touch tuning. hold_channel_gesture() logs its own
// per-channel detail, so HOLD is deliberately not logged here as well.
static const char* iqs323_event_name(iqs323_gesture_events ev) {
    switch (ev) {
    case IQS323_GESTURE_NONE:           return "none";
    case IQS323_GESTURE_TAP:            return "tap";
    case IQS323_GESTURE_SWIPE_POSITIVE: return "swipe+";
    case IQS323_GESTURE_SWIPE_NEGATIVE: return "swipe-";
    case IQS323_GESTURE_FLICK_POSITIVE: return "flick+";
    case IQS323_GESTURE_FLICK_NEGATIVE: return "flick-";
    case IQS323_GESTURE_HOLD:           return "hold";
    default:                            return "?";
    }
}

static Gesture translate_gesture(iqs323_gesture_events ev, IQS323& iqs) {
    // HOLD logs inside hold_channel_gesture() with its channel deflections;
    // everything else gets its line here so swipes/flicks/taps are visible too.
    if (ev != IQS323_GESTURE_HOLD) {
        Log_info("clock91: gesture event=%s(%d)", iqs323_event_name(ev), (int)ev);
    }

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
    } else {
        // The sensor signalled but no gesture had matured by the time we read.
        // Normal for a press still forming; a run of these with no gesture
        // following is the signature of a mistuned threshold.
        Log_info("clock91: touch signalled, no slider event");
    }
    iqs323_task_i2c_unlock();
    return result;
}

// ── Wake events ──
//
// One binary semaphore is the whole wake path. Two things can end a wait: the
// touch sensor firing, or the deadline passing. xSemaphoreTake with a timeout
// distinguishes them in a single blocking call — pdTRUE means a signal arrived,
// pdFALSE means the timeout expired — so callers never have to infer the cause
// afterwards.
//
// Why a semaphore rather than a task notification: the notification API targets
// a specific TaskHandle_t, which means whoever signals has to know who is
// waiting. That coupling is what made the old code fragile. The semaphore is
// addressed by the event, not the waiter, so the producer side stays correct no
// matter which task ends up calling clock91_wait().
//
// Binary, not counting: a burst of sensor activity during one wait should
// produce one wake, not a backlog that spins the loop once per event. The
// semaphore also latches, so a touch landing while we are busy is remembered
// and taken immediately by the next wait instead of being lost.
static SemaphoreHandle_t wake_sem = NULL;

typedef enum {
    WAKE_TIMEOUT = 0,  // deadline expired, no touch
    WAKE_TOUCH,        // sensor signalled
} WakeReason;

static void clock91_wake_init(void) {
    if (wake_sem == NULL) {
        wake_sem = xSemaphoreCreateBinary();
        configASSERT(wake_sem != NULL);
    }
}

// Signal a touch. Called from the IQS323 *task* (not an ISR — see
// iqs323_task_set_data_callback), with that task's mutex held, so this must not
// block and must not touch I2C. xSemaphoreGive satisfies both: it is O(1), it
// never waits, and on an already-signalled binary semaphore it is a no-op
// rather than an error.
static void clock91_wake_signal_touch(void) {
    touch_pending = true;
    if (wake_sem) xSemaphoreGive(wake_sem);
}

// Runs in IQS323 task context with that task's mutex held — keep it brief and
// non-blocking; see clock91_wake_signal_touch().
static void on_iqs323_data(void) {
    clock91_wake_signal_touch();
}

// Block until a touch arrives or `seconds` elapse, and report which happened.
//
// The chip is not put to sleep here. Under CONFIG_PM_ENABLE with
// CONFIG_FREERTOS_USE_TICKLESS_IDLE, the IDF idle task drops the SoC into light
// sleep once every task is blocked and restores it for the next deadline.
// Blocking properly is therefore the entire job — any busy-wait here would
// defeat the PM framework by keeping a runnable task on the scheduler.
static WakeReason clock91_wait_ms(uint32_t ms) {
    clock91_wake_init();

    return xSemaphoreTake(wake_sem, pdMS_TO_TICKS(ms)) == pdTRUE
               ? WAKE_TOUCH
               : WAKE_TIMEOUT;
}

static WakeReason clock91_wait(uint32_t seconds) {
    return clock91_wait_ms(seconds * 1000U);
}

// Discard any gesture that is already queued, signal included.
//
// Clearing touch_pending alone is not enough: the semaphore latches, so a
// dropped touch would still satisfy the next clock91_wait() immediately and
// burn a cycle on a gesture that was deliberately thrown away. Both halves of
// the wake state have to be cleared together, which is why this is a helper
// rather than an open-coded assignment at each site.
static void clock91_wake_discard(void) {
    clock91_wake_init();
    touch_pending = false;
    xSemaphoreTake(wake_sem, 0);
}

// ── WiFi ──

// Park the WiFi link in DTIM-based modem sleep instead of dropping the
// association.
//
// This used to call WiFi.disconnect(false) after every cycle and reassociate on
// the next one. Staying associated removes that reconnect (DHCP + ARP, and a
// full auth/assoc handshake) from every 15-minute full cycle, and lets the AP
// buffer for us rather than treating each cycle as a new station.
//
// WIFI_PS_MAX_MODEM is what tells the AP we are power-saving: the station sets
// the power-management bit in its frames, so the AP holds our traffic and
// announces it in the TIM. The radio then only wakes on our DTIM beacon instead
// of staying in receive. WIFI_PS_MIN_MODEM wakes every beacon, which is most of
// the receive cost with none of the latency benefit here, so MAX is the right
// end of that trade for a device that talks once per quarter hour.
//
// Note the driver is deliberately never deinitialised. Any route into
// esp_wifi_deinit() hangs on this build (CONFIG_ESP_COEX_SW_COEXIST_ENABLE=y
// with CONFIG_BT_ENABLED=y): the task stops making progress and dies to the
// watchdog, with no panic to point at it. Both WiFi.mode(WIFI_OFF) and
// WiFi.disconnect(true) reach it. That is why this parks the link rather than
// closing it.
static void clock91_wifi_powersave(void) {
    if (WiFi.getMode() == WIFI_MODE_NULL) return;

    esp_err_t err = esp_wifi_set_ps(WIFI_PS_MAX_MODEM);
    if (err != ESP_OK) {
        Log_error("clock91: esp_wifi_set_ps(MAX_MODEM) failed: %d", (int)err);
        return;
    }

    Log_info("clock91: WiFi power-save on (associated=%d, DTIM modem sleep)",
             WiFi.status() == WL_CONNECTED);
}

// Leave power-save for the duration of a transfer.
//
// MAX_MODEM adds up to a DTIM interval of latency to every received frame,
// which compounds badly across the many round trips of a TLS handshake plus the
// FMI fetch. Callers must pair this with clock91_wifi_powersave() so the link
// goes back to sleeping — see clock91_full_cycle().
static void clock91_wifi_active(void) {
    if (WiFi.getMode() == WIFI_MODE_NULL) return;

    esp_err_t err = esp_wifi_set_ps(WIFI_PS_NONE);
    if (err != ESP_OK) {
        Log_error("clock91: esp_wifi_set_ps(NONE) failed: %d", (int)err);
    }
}

// Cached result of WifiCaptivePortal.isSaved().
//
// isSaved() re-reads all of NVS to answer one question: does slot 0 have an
// SSID? It calls readWifiCredentials(), which sweeps WIFI_MAX_SAVED_CREDS slots
// x 11 keys. Most of those keys are unset on a normal install (WPA-enterprise
// and static-IP fields), and Preferences logs every miss at ERROR level, so a
// single call produced ~35 nvs_get_str error lines per cycle — noise that
// CORE_DEBUG_LEVEL cannot filter because it is logged as an error.
//
// Credentials only change by going through the captive portal, so the answer is
// stable for the life of the boot. Cache it, and let clock91_start_portal()
// invalidate the cache when it has actually run.
static int wifi_saved_cache = -1;  // -1 unknown, 0 no, 1 yes

static void clock91_wifi_invalidate_saved(void) { wifi_saved_cache = -1; }

static bool clock91_wifi_saved(void) {
    if (wifi_saved_cache < 0) {
        wifi_saved_cache = WifiCaptivePortal.isSaved() ? 1 : 0;
    }
    return wifi_saved_cache == 1;
}

static bool clock91_wifi_connect(void) {
    // Check the association before the credential lookup: when the link is
    // already up from a previous cycle, whether credentials are on disk is not
    // a question worth asking.
    if (WiFi.status() == WL_CONNECTED) {
        clock91_wifi_active();
        Log_info("clock91: WiFi already associated, IP=%s RSSI=%d",
                 WiFi.localIP().toString().c_str(), WiFi.RSSI());
        return true;
    }

    if (!clock91_wifi_saved()) {
        Log_info("clock91: no WiFi credentials (use hold gesture to configure)");
        return false;
    }

    WiFi.mode(WIFI_STA);

    // Keep the association across our own light sleeps: without this the driver
    // drops the link whenever the station goes idle, which is exactly what we
    // are trying to avoid.
    WiFi.setAutoReconnect(true);

    int res = WifiCaptivePortal.autoConnect();
    if (!res) {
        // Deliberately no WiFi.mode(WIFI_OFF) here: it routes into
        // esp_wifi_deinit(), which hangs this build under WiFi/BLE coexistence
        // (see clock91_wifi_powersave). Park the idle driver in power-save
        // instead, and let the next cycle retry the association.
        Log_error("clock91: WiFi connect failed (status=%d)", WiFi.status());
        clock91_wifi_powersave();
        return false;
    }

    clock91_wifi_active();

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
        clock91_wifi_powersave();
        Log_info("TRACE: wifi powersave done");
    }

    // BLE scan (runs after WiFi is parked in modem sleep — they share the
    // radio, and coexistence arbitrates between them)
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

// Normal screen with no fetched data: real clock/date/station, dashes elsewhere.
// Used to acknowledge a SETUP exit before the slow post-portal work.
static void clock91_render_normal_screen(void) {
    struct tm ti;
    time_t now = time(NULL);
    localtime_r(&now, &ti);

    int station_index = preferences.getUInt("station_idx", 0);
    if (station_index >= STATION_COUNT) station_index = 0;

    DisplayState state = {};
    state.hour = ti.tm_hour;
    state.minute = ti.tm_min;
    state.day = ti.tm_mday;
    state.month = ti.tm_mon + 1;
    state.wday = ti.tm_wday;
    state.station_name = STATIONS[station_index].name;
    state.solar_w = NAN;
    state.ac_w = NAN;
    state.house_w = NAN;
    state.engine_v = NAN;
    state.saloon_temp = NAN;
    state.saloon_humidity = NAN;
    state.icebox_temp = NAN;
    state.battery_pct = -1;
    state.device_pct = -1;
    state.timer_active = false;
    state.otg_enabled = otg_enabled;
    state.days = NULL;
    state.history = NULL;

    buildLayoutInto(g_dl, state);
    DrawList& dl = g_dl;
    renderFull(dl);

    prev_hour = ti.tm_hour;
    prev_minute = ti.tm_min;
}

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
    // WifiCaptive polls this in a tight loop. touch_pending is set by the RDY
    // interrupt as soon as any data is ready, long before a HOLD matures, so
    // clearing it on every poll threw the press away before it became a gesture.
    // Only consume it once getSliderEvent() actually returns one, and rate-limit
    // the poll: getSliderEvent() is read-and-clear and races the sensor latch.
    {
        uint32_t portal_start = millis();
        WifiCaptivePortal.setAbortCallback([portal_start]() -> bool {
            static uint32_t last_poll_ms = 0;

            // The opening hold is often still in progress here.
            if (millis() - portal_start < 5000) {
                touch_pending = false;
                return false;
            }
            if (!touch_pending) return false;

            uint32_t now = millis();
            if (now - last_poll_ms < 100) return false;
            last_poll_ms = now;

            iqs323_task_i2c_lock();
            bool has_gesture = iqs323.getSliderEvent();
            iqs323_gesture_events ev =
                has_gesture ? iqs323.getGestureType() : IQS323_GESTURE_NONE;
            iqs323_task_i2c_unlock();

            // Leave touch_pending set so a still-forming hold gets another poll.
            if (!has_gesture) return false;
            touch_pending = false;

            Log_info("clock91: portal abort poll: gesture ev=%d", (int)ev);

            if (ev == IQS323_GESTURE_HOLD) {
                Log_info("clock91: portal cancelled by hold gesture");

                // Paint here, before startPortal()'s teardown and its ~15 s
                // credential-less reconnect, so the gesture is acknowledged
                // immediately instead of ~35 s later.
                clock91_render_normal_screen();
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
        clock91_wifi_powersave();
    } else {
        Log_info("clock91: portal done, no WiFi");
    }

    // The portal may have written new credentials, so the cached isSaved()
    // answer is no longer valid.
    clock91_wifi_invalidate_saved();

    ble_config_reload();

    // Drop any gesture that arrived while the portal was up — otherwise the
    // hold that opened it is still queued and immediately reopens it.
    last_portal_exit_ms = millis();
    clock91_wake_discard();
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

    clock91_wake_discard();

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

        // Wait for the next whole second boundary, or a gesture, whichever
        // comes first. This waits in milliseconds: the old second-granularity
        // helper rounded every sub-second remainder up to a full second, so the
        // countdown drifted later on every tick.
        uint32_t next_s_ms = start_ms + ((uint32_t)(elapsed_s + 1) * 1000);
        uint32_t now_ms = millis();
        if (next_s_ms > now_ms) {
            clock91_wait_ms(next_s_ms - now_ms);
        }
    }

    if (timer.buzzing) {
        Log_info("clock91: timer done — buzzing");
        buzzer_timer();
    }
}

// ── Power logging ──

// One coulomb-counter reading per cycle, so autonomy can be measured from the
// serial log without a lab PSU.
//
// capacityRemain (REMAIN_UF) is the number that matters: it is coulomb-counted,
// so unlike the displayed percentage it is not affected by BYPASS_BQ27427_SOC
// deriving SOC from voltage. Unfiltered rather than REMAIN because the filtered
// value is smoothed for display and lags the small per-cycle deltas this is
// meant to expose.
//
// current() is signed: negative = discharging, positive = charging. It is an
// average over the gauge's own window, not an instantaneous reading, so a
// single sample taken during our ~1 s awake window is not the awake current —
// only the trend across many cycles is meaningful.
static void clock91_log_power(void) {
    if (!lipo._initialized) {
        Log_info("clock91: power gauge=uninit");
        return;
    }

    // flags() carries ITPOR: set when Impedance Track has been reset and its
    // SOC output is not yet trustworthy. design= is logged alongside full= to
    // show whether the gauge has learned a real full capacity or is still
    // reporting the configured design value.
    uint16_t fl = lipo.flags();
    Log_info("clock91: power remain=%umAh full=%umAh design=%umAh v=%umV i=%dmA "
             "itsoc=%u%% flags=0x%04X%s%s%s",
             (unsigned)lipo.capacity(REMAIN_UF),
             (unsigned)lipo.capacity(FULL),
             (unsigned)lipo.capacity(DESIGN),
             (unsigned)lipo.voltage(),
             (int)lipo.current(AVG),
             (unsigned)lipo.soc(FILTERED),
             (unsigned)fl,
             (fl & BQ27427_FLAG_ITPOR) ? " ITPOR" : "",
             (fl & BQ27427_FLAG_FC)    ? " FC"    : "",
             (fl & BQ27427_FLAG_DSG)   ? " DSG"   : "");
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

    // Create the wake semaphore before registering the callback, so the first
    // signal cannot arrive with wake_sem still NULL.
    clock91_wake_discard();
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

    Log_info("clock91: waiting %lu s", (unsigned long)sleep_secs);
    WakeReason reason = clock91_wait(sleep_secs);

    // Poll whenever touch_pending is set, not only when this wait was the one
    // that saw the touch: a gesture that landed while the previous cycle was
    // still rendering sets the flag without any wait observing it. The wake
    // reason is only used to label the log line.
    Gesture gesture = GESTURE_NONE;
    if (touch_pending) {
        gesture = clock91_poll_gesture();
        Log_info("clock91: gesture=%d (%s wake)", gesture,
                 reason == WAKE_TOUCH ? "touch" : "timer");
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

    clock91_log_power();

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
