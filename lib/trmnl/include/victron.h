#pragma once

#include <cstdint>
#include <cstddef>
#include <cmath>

// Victron Instant Readout BLE advertisement parser.
// Reference: https://github.com/keshavdv/victron-ble
//            https://github.com/Fabian-Schmidt/esphome-victron_ble

// Record types in decrypted payload
enum VictronRecordType : uint8_t {
    VICTRON_SOLAR_CHARGER = 0x01,
    VICTRON_BATTERY_MONITOR = 0x02,
    VICTRON_INVERTER = 0x03,
    VICTRON_DC_DC_CONVERTER = 0x04,
    VICTRON_SMART_LITHIUM = 0x05,
    VICTRON_INVERTER_RS = 0x06,
    VICTRON_GX_DEVICE = 0x07,
    VICTRON_AC_CHARGER = 0x08,
    VICTRON_SMART_BATTERY_PROTECT = 0x09,
    VICTRON_LYNX_SMART_BMS = 0x0A,
    VICTRON_VE_BUS = 0x0C,
};

// Parsed SmartSolar data
struct VictronSolar {
    float pv_power;         // W — solar panel input
    float battery_voltage;  // V
    float battery_current;  // A — charger output
    uint8_t charge_state;   // 0=off, 2=fault, 3=bulk, 4=absorption, 5=float
    bool valid;
};

// Parsed SmartShunt data
struct VictronShunt {
    float battery_voltage;  // V
    float battery_current;  // A (negative = discharge)
    float aux_voltage;      // V — starter/engine battery
    float soc;              // % (0-100), NAN if unavailable
    int consumed_ah;        // Ah consumed
    bool valid;
};

// Decrypt a Victron BLE advertisement.
// mfr_data: full manufacturer-specific data (after company ID 0x02E1)
// mfr_len: length of manufacturer data
// key: 16-byte AES encryption key
// plain: output buffer for decrypted payload (at least 16 bytes)
// plain_len: output length of decrypted data
// Returns true on success.
bool victronDecrypt(const uint8_t* mfr_data, size_t mfr_len,
                    const uint8_t* key,
                    uint8_t* plain, size_t* plain_len);

// Parse decrypted SmartSolar payload
VictronSolar parseVictronSolar(const uint8_t* decrypted, size_t len);

// Parse decrypted SmartShunt payload
VictronShunt parseVictronShunt(const uint8_t* decrypted, size_t len);

// Get the record type from decrypted data (first byte)
VictronRecordType victronRecordType(const uint8_t* mfr_data, size_t mfr_len);
