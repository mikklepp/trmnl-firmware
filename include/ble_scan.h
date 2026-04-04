#pragma once

#ifdef CLOCK91_MODE

#include "victron.h"
#include "ruuvi.h"

// BLE scan results — populated by ble_scan_run()
struct BleScanResult {
    VictronSolar solar;
    VictronShunt shunt;
    RuuviData ruuvi_saloon;
    RuuviData ruuvi_icebox;
};

// Run a BLE scan for the specified duration (seconds).
// Filters for configured Victron and Ruuvi devices.
// Returns results with .valid flags indicating which devices were heard.
BleScanResult ble_scan_run(int duration_seconds);

#endif // CLOCK91_MODE
