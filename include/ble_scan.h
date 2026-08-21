#pragma once

#ifdef CLOCK91_MODE

#include "victron.h"
#include "ruuvi.h"

// BLE scan results — populated by ble_scan_run()
struct BleScanResult {
    VictronSolar solar;
    VictronShunt shunt;
    VictronVEBus vebus;
    RuuviData ruuvi_saloon;
    RuuviData ruuvi_icebox;
};

// Load BLE device config from NVS. Call once at startup.
void ble_config_init(void);

// Reload BLE device config from NVS (e.g. after captive portal saves).
void ble_config_reload(void);

// Run a BLE scan for the specified duration (seconds).
// Filters for configured Victron and Ruuvi devices.
// Returns results with .valid flags indicating which devices were heard.
BleScanResult ble_scan_run(int duration_seconds);

#endif // CLOCK91_MODE
