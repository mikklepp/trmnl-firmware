#include "ble_scan.h"

#ifdef CLOCK91_MODE

#include <string>
#include <NimBLEDevice.h>
#include <Preferences.h>
#include <freertos/semphr.h>
#include <trmnl_log.h>
#include "victron.h"
#include "ruuvi.h"

extern Preferences preferences;

// Victron company ID
static const uint16_t VICTRON_COMPANY_ID = 0x02E1;
// Ruuvi company ID
static const uint16_t RUUVI_COMPANY_ID = 0x0499;

// Device config loaded from NVS
static uint8_t victron_solar_key[16] = {};
static uint8_t victron_shunt_key[16] = {};
static uint8_t victron_vebus_key[16] = {};
static std::string victron_solar_mac;
static std::string victron_shunt_mac;
static std::string victron_vebus_mac;
static std::string ruuvi_saloon_mac;
static std::string ruuvi_icebox_mac;
static bool has_solar = false;
static bool has_shunt = false;
static bool has_vebus = false;
static bool has_ruuvi_saloon = false;
static bool has_ruuvi_icebox = false;

// Scan results (written by callback, guarded by scan_mutex)
static BleScanResult scan_result;
static uint8_t devices_heard = 0;
static uint8_t devices_expected_mask = 0;  // bitmask of configured devices
static SemaphoreHandle_t scan_mutex = NULL;

// Parse a hex string "AABBCCDD..." into bytes. Returns number of bytes parsed.
static int hex_to_bytes(const char* hex, uint8_t* out, int max_len) {
    int len = 0;
    while (*hex && *(hex + 1) && len < max_len) {
        char hi = *hex++, lo = *hex++;
        auto nibble = [](char c) -> uint8_t {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return 0;
        };
        out[len++] = (nibble(hi) << 4) | nibble(lo);
    }
    return len;
}

static void load_ble_config(void) {
    has_solar = has_shunt = has_vebus = has_ruuvi_saloon = has_ruuvi_icebox = false;

    // Victron Solar: MAC + 16-byte AES key (stored as 32-char hex)
    String solar_mac_str = preferences.getString("v_solar_mac", "");
    String solar_key_str = preferences.getString("v_solar_key", "");
    if (solar_mac_str.length() > 0 && solar_key_str.length() == 32) {
        victron_solar_mac = solar_mac_str.c_str();
        hex_to_bytes(solar_key_str.c_str(), victron_solar_key, 16);
        has_solar = true;
        Log_info("BLE: Victron Solar configured: %s", victron_solar_mac.c_str());
    }

    // Victron Shunt: MAC + key
    String shunt_mac_str = preferences.getString("v_shunt_mac", "");
    String shunt_key_str = preferences.getString("v_shunt_key", "");
    if (shunt_mac_str.length() > 0 && shunt_key_str.length() == 32) {
        victron_shunt_mac = shunt_mac_str.c_str();
        hex_to_bytes(shunt_key_str.c_str(), victron_shunt_key, 16);
        has_shunt = true;
        Log_info("BLE: Victron Shunt configured: %s", victron_shunt_mac.c_str());
    }

    // Victron VE.Bus (MultiPlus): MAC + key
    String vebus_mac_str = preferences.getString("v_vebus_mac", "");
    String vebus_key_str = preferences.getString("v_vebus_key", "");
    if (vebus_mac_str.length() > 0 && vebus_key_str.length() == 32) {
        victron_vebus_mac = vebus_mac_str.c_str();
        hex_to_bytes(vebus_key_str.c_str(), victron_vebus_key, 16);
        has_vebus = true;
        Log_info("BLE: Victron VE.Bus configured: %s", victron_vebus_mac.c_str());
    }

    // Ruuvi tags: MAC only (no encryption)
    String saloon_mac_str = preferences.getString("ruuvi_0_mac", "");
    if (saloon_mac_str.length() > 0) {
        ruuvi_saloon_mac = saloon_mac_str.c_str();
        has_ruuvi_saloon = true;
        Log_info("BLE: Ruuvi Saloon configured: %s", ruuvi_saloon_mac.c_str());
    }

    String icebox_mac_str = preferences.getString("ruuvi_1_mac", "");
    if (icebox_mac_str.length() > 0) {
        ruuvi_icebox_mac = icebox_mac_str.c_str();
        has_ruuvi_icebox = true;
        Log_info("BLE: Ruuvi Icebox configured: %s", ruuvi_icebox_mac.c_str());
    }

    devices_expected_mask = (has_solar ? 0x01 : 0) | (has_shunt ? 0x02 : 0)
                          | (has_vebus ? 0x10 : 0)
                          | (has_ruuvi_saloon ? 0x04 : 0) | (has_ruuvi_icebox ? 0x08 : 0);
    Log_info("BLE: %d devices configured (mask=0x%02X)",
             __builtin_popcount(devices_expected_mask), devices_expected_mask);
}

static void handle_victron(const std::string& mac, const uint8_t* mfr, size_t mfr_len) {
    // mfr data starts after company ID (already stripped by caller)
    uint8_t plain[32];
    size_t plain_len = 0;

    if (has_solar && mac == victron_solar_mac) {
        if (victronDecrypt(mfr, mfr_len, victron_solar_key, plain, &plain_len)) {
            VictronSolar parsed = parseVictronSolar(plain, plain_len);
            if (parsed.valid) {
                xSemaphoreTake(scan_mutex, portMAX_DELAY);
                scan_result.solar = parsed;
                devices_heard |= 0x01;
                xSemaphoreGive(scan_mutex);
            }
        }
    }

    if (has_shunt && mac == victron_shunt_mac) {
        if (victronDecrypt(mfr, mfr_len, victron_shunt_key, plain, &plain_len)) {
            VictronShunt parsed = parseVictronShunt(plain, plain_len);
            if (parsed.valid) {
                xSemaphoreTake(scan_mutex, portMAX_DELAY);
                scan_result.shunt = parsed;
                devices_heard |= 0x02;
                xSemaphoreGive(scan_mutex);
            }
        }
    }

    if (has_vebus && mac == victron_vebus_mac) {
        if (victronDecrypt(mfr, mfr_len, victron_vebus_key, plain, &plain_len)) {
            VictronVEBus parsed = parseVictronVEBus(plain, plain_len);
            if (parsed.valid) {
                xSemaphoreTake(scan_mutex, portMAX_DELAY);
                scan_result.vebus = parsed;
                devices_heard |= 0x10;
                xSemaphoreGive(scan_mutex);
            }
        }
    }
}

static void handle_ruuvi(const std::string& mac, const uint8_t* payload, size_t len) {
    if (has_ruuvi_saloon && mac == ruuvi_saloon_mac) {
        RuuviData parsed = parseRuuviRAWv2(payload, len);
        if (parsed.valid) {
            xSemaphoreTake(scan_mutex, portMAX_DELAY);
            scan_result.ruuvi_saloon = parsed;
            devices_heard |= 0x04;
            xSemaphoreGive(scan_mutex);
        }
    }

    if (has_ruuvi_icebox && mac == ruuvi_icebox_mac) {
        RuuviData parsed = parseRuuviRAWv2(payload, len);
        if (parsed.valid) {
            xSemaphoreTake(scan_mutex, portMAX_DELAY);
            scan_result.ruuvi_icebox = parsed;
            devices_heard |= 0x08;
            xSemaphoreGive(scan_mutex);
        }
    }
}

class Clock91ScanCallbacks : public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice* device) override {
        if (!device->haveManufacturerData()) return;

        std::string mfr = device->getManufacturerData();
        if (mfr.size() < 4) return;

        uint16_t company = (uint8_t)mfr[0] | ((uint8_t)mfr[1] << 8);
        std::string mac = device->getAddress().toString();

        // Payload after company ID
        const uint8_t* payload = reinterpret_cast<const uint8_t*>(mfr.data() + 2);
        size_t payload_len = mfr.size() - 2;

        if (company == VICTRON_COMPANY_ID) {
            handle_victron(mac, payload, payload_len);
        } else if (company == RUUVI_COMPANY_ID) {
            handle_ruuvi(mac, payload, payload_len);
        }
    }

    void onScanEnd(const NimBLEScanResults& results, int reason) override {
        Log_info("BLE: scan ended, reason=%d, heard=0x%02X/0x%02X",
                 reason, devices_heard, devices_expected_mask);
    }
};

static Clock91ScanCallbacks scanCallbacks;

BleScanResult ble_scan_run(int duration_seconds) {
    scan_result = {};
    devices_heard = 0;

    if (!scan_mutex) {
        scan_mutex = xSemaphoreCreateMutex();
    }

    if (devices_expected_mask == 0) {
        Log_info("BLE: no devices configured, skipping scan");
        return scan_result;
    }

    Log_info("BLE: starting %ds scan for %d devices (mask=0x%02X)",
             duration_seconds, __builtin_popcount(devices_expected_mask), devices_expected_mask);

    NimBLEDevice::init("");
    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setScanCallbacks(&scanCallbacks, true);  // true = report duplicates
    scan->setActiveScan(false);
    scan->setInterval(100);
    scan->setWindow(99);

    // Blocking scan with early stop check
    scan->start(0);  // non-blocking, scan forever

    unsigned long deadline = millis() + (duration_seconds * 1000);
    while (millis() < deadline) {
        xSemaphoreTake(scan_mutex, portMAX_DELAY);
        bool all_heard = (devices_heard & devices_expected_mask) == devices_expected_mask;
        xSemaphoreGive(scan_mutex);
        if (all_heard) {
            Log_info("BLE: all devices heard, stopping early");
            break;
        }
        delay(100);
    }

    scan->stop();
    scan->clearResults();
    NimBLEDevice::deinit(true);

    Log_info("BLE: scan complete, heard=0x%02X/0x%02X (%d/%d devices)",
             devices_heard, devices_expected_mask,
             __builtin_popcount(devices_heard & devices_expected_mask),
             __builtin_popcount(devices_expected_mask));
    return scan_result;
}

void ble_config_init(void) {
    load_ble_config();
}

void ble_config_reload(void) {
    load_ble_config();
}

#endif // CLOCK91_MODE
