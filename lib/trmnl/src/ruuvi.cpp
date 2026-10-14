#include "ruuvi.h"
#include "trmnl_log.h"

// Ruuvi RAWv2 (Data Format 5) specification:
// https://docs.ruuvi.com/communication/bluetooth-advertisements/data-format-5-rawv2
//
// Byte offsets (after company ID):
//  0:     Data format (0x05)
//  1-2:   Temperature (signed, 0.005 °C per unit, 0x8000 = invalid)
//  3-4:   Humidity (unsigned, 0.0025 % per unit, 0xFFFF = invalid)
//  5-6:   Pressure (unsigned, Pa, add 50000, 0xFFFF = invalid)
//  7-8:   Acceleration X (signed, mG)
//  9-10:  Acceleration Y (signed, mG)
// 11-12:  Acceleration Z (signed, mG)
// 13-14:  Battery voltage (bits 15-5) + TX power (bits 4-0)
//         Voltage: value >> 5, + 1600 mV
//         TX power: (value & 0x1F) * 2 - 40 dBm
// 15:     Movement counter
// 16-17:  Measurement sequence
// 18-23:  MAC address (not parsed here)

static uint16_t read_u16(const uint8_t* p) {
    return (uint16_t)(p[0] << 8) | p[1];
}

static int16_t read_i16(const uint8_t* p) {
    return (int16_t)read_u16(p);
}

RuuviData parseRuuviRAWv2(const uint8_t* data, size_t len) {
    RuuviData result = {};
    result.valid = false;

    if (len < 24) {
        Log_error("Ruuvi: payload too short (%zu bytes, need 24)", len);
        return result;
    }
    if (data[0] != 0x05) {
        Log_error("Ruuvi: unexpected format 0x%02X (expected 0x05)", data[0]);
        return result;
    }

    // Temperature
    int16_t raw_temp = read_i16(data + 1);
    if (raw_temp == (int16_t)0x8000) {
        result.temperature = NAN;
    } else {
        result.temperature = raw_temp * 0.005f;
    }

    // Humidity
    uint16_t raw_hum = read_u16(data + 3);
    if (raw_hum == 0xFFFF) {
        result.humidity = NAN;
    } else {
        result.humidity = raw_hum * 0.0025f;
    }

    // Pressure
    uint16_t raw_pres = read_u16(data + 5);
    if (raw_pres == 0xFFFF) {
        result.pressure = 0;
    } else {
        result.pressure = raw_pres;  // add 50000 to get Pa, divide by 100 for hPa
    }

    // Battery voltage + TX power (packed in bytes 13-14)
    uint16_t raw_power = read_u16(data + 13);
    if (raw_power == 0xFFFF) {
        result.battery_voltage = NAN;
        result.tx_power = -128;
    } else {
        result.battery_voltage = (raw_power >> 5) / 1000.0f + 1.6f;
        result.tx_power = (int8_t)((raw_power & 0x1F) * 2 - 40);
    }

    // Movement counter
    result.movement_counter = data[15];

    // Measurement sequence
    result.measurement_sequence = read_u16(data + 16);

    result.valid = true;
    Log_info("Ruuvi: %.2fC %.1f%% bat=%.3fV seq=%u",
             result.temperature, result.humidity,
             result.battery_voltage, result.measurement_sequence);
    return result;
}
