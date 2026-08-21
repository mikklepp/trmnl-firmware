#pragma once

#include <cstdint>
#include <cstddef>
#include <cmath>

struct RuuviData {
    float temperature;    // °C, resolution 0.005
    float humidity;       // %, resolution 0.0025
    uint16_t pressure;    // Pa (raw), add 50000 to get hPa * 100
    float battery_voltage;// V
    int8_t tx_power;      // dBm
    uint8_t movement_counter;
    uint16_t measurement_sequence;
    // MAC not stored here — used for filtering before parse

    bool valid;
};

// Parse Ruuvi RAWv2 (Data Format 5) from manufacturer-specific BLE advertisement data.
// Input: pointer to the manufacturer data payload AFTER the 2-byte company ID (0x0499).
// The first byte should be 0x05 (format identifier).
// Length must be at least 24 bytes (format byte + 23 bytes of data).
RuuviData parseRuuviRAWv2(const uint8_t* data, size_t len);
