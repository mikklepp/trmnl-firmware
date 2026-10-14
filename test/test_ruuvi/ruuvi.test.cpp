#include <unity.h>
#include <ruuvi.h>
#include <cstring>
#include <cmath>

void setUp(void) {}
void tearDown(void) {}

// Test vector from Ruuvi documentation:
// https://docs.ruuvi.com/communication/bluetooth-advertisements/data-format-5-rawv2#test-vectors

void test_ruuvi_valid_max_values(void) {
    // Maximum values test vector
    // Temp: 163.835°C, Humidity: 163.8350%, Pressure: 115534 Pa
    // Acc: 32767 mG each, Voltage: 3.646V, TX: +24 dBm
    // Movement: 254, Sequence: 65534
    uint8_t data[] = {
        0x05,                   // Format
        0x7F, 0xFF,             // Temperature: 32767 = 163.835°C
        0xFF, 0xFE,             // Humidity: 65534 = 163.8350%
        0xFF, 0xFE,             // Pressure: 65534 Pa (+50000 = 115534)
        0x7F, 0xFF,             // Acc X: 32767
        0x7F, 0xFF,             // Acc Y: 32767
        0x7F, 0xFF,             // Acc Z: 32767
        0xFF, 0xDE,             // Power: voltage 2046 + 1600 = 3646mV, tx (30 & 0x1F)*2-40 = 20
        0xFE,                   // Movement: 254
        0xFF, 0xFE,             // Sequence: 65534
        0xCB, 0xB8, 0x33, 0x4C, 0x88, 0x4F  // MAC
    };

    RuuviData r = parseRuuviRAWv2(data, sizeof(data));
    TEST_ASSERT_TRUE(r.valid);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 163.835f, r.temperature);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 163.835f, r.humidity);
    TEST_ASSERT_EQUAL_UINT16(65534, r.pressure);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.646f, r.battery_voltage);
    TEST_ASSERT_EQUAL_INT8(20, r.tx_power);
    TEST_ASSERT_EQUAL_UINT8(254, r.movement_counter);
    TEST_ASSERT_EQUAL_UINT16(65534, r.measurement_sequence);
}

void test_ruuvi_valid_min_values(void) {
    // Minimum values test vector
    // Temp: -163.835°C, Humidity: 0%, Pressure: 50000 Pa
    uint8_t data[] = {
        0x05,
        0x80, 0x01,             // Temperature: -32767 = -163.835°C
        0x00, 0x00,             // Humidity: 0
        0x00, 0x00,             // Pressure: 0 (+50000 = 50000 Pa)
        0x80, 0x01,             // Acc X: -32767
        0x80, 0x01,             // Acc Y: -32767
        0x80, 0x01,             // Acc Z: -32767
        0x00, 0x00,             // Power: voltage 0+1600=1600mV, tx 0*2-40=-40
        0x00,                   // Movement: 0
        0x00, 0x00,             // Sequence: 0
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };

    RuuviData r = parseRuuviRAWv2(data, sizeof(data));
    TEST_ASSERT_TRUE(r.valid);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -163.835f, r.temperature);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, r.humidity);
    TEST_ASSERT_EQUAL_UINT16(0, r.pressure);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.6f, r.battery_voltage);
    TEST_ASSERT_EQUAL_INT8(-40, r.tx_power);
}

void test_ruuvi_invalid_values(void) {
    // Invalid values test vector (all fields 0x8000/0xFFFF)
    uint8_t data[] = {
        0x05,
        0x80, 0x00,             // Temperature: invalid
        0xFF, 0xFF,             // Humidity: invalid
        0xFF, 0xFF,             // Pressure: invalid
        0x80, 0x00,             // Acc X: invalid
        0x80, 0x00,             // Acc Y: invalid
        0x80, 0x00,             // Acc Z: invalid
        0xFF, 0xFF,             // Power: invalid
        0x00,
        0xFF, 0xFF,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };

    RuuviData r = parseRuuviRAWv2(data, sizeof(data));
    TEST_ASSERT_TRUE(r.valid);
    TEST_ASSERT_TRUE(isnan(r.temperature));
    TEST_ASSERT_TRUE(isnan(r.humidity));
    TEST_ASSERT_TRUE(isnan(r.battery_voltage));
}

void test_ruuvi_typical_indoor(void) {
    // Typical indoor reading: ~21.3°C, 45% humidity
    // Temp: 21.3 / 0.005 = 4260 = 0x10A4
    // Humidity: 45.0 / 0.0025 = 18000 = 0x4650
    // Pressure: 51325 - 50000 = 1325 = 0x052D
    uint8_t data[] = {
        0x05,
        0x10, 0xA4,             // Temperature: 21.3°C
        0x46, 0x50,             // Humidity: 45.0%
        0x05, 0x2D,             // Pressure: 1325 (+50000 = 101325 Pa)
        0x00, 0x00,
        0x00, 0x00,
        0x00, 0x00,
        0x0C, 0x04,             // Voltage ~3.0V, TX -32 dBm
        0x05,
        0x00, 0x42,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };

    RuuviData r = parseRuuviRAWv2(data, sizeof(data));
    TEST_ASSERT_TRUE(r.valid);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 21.3f, r.temperature);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 45.0f, r.humidity);
}

void test_ruuvi_wrong_format(void) {
    uint8_t data[] = { 0x03, 0x00, 0x00 };  // Format 3, not 5
    RuuviData r = parseRuuviRAWv2(data, 24);
    TEST_ASSERT_FALSE(r.valid);
}

void test_ruuvi_too_short(void) {
    uint8_t data[] = { 0x05, 0x10, 0xA4 };
    RuuviData r = parseRuuviRAWv2(data, 3);
    TEST_ASSERT_FALSE(r.valid);
}

void test_ruuvi_negative_temperature(void) {
    // -2.1°C / 0.005 = -420 = 0xFE5C (two's complement)
    uint8_t data[24] = {};
    data[0] = 0x05;
    data[1] = 0xFE; data[2] = 0x5C;  // -420 = -2.1°C
    data[3] = 0x00; data[4] = 0x00;
    data[5] = 0x00; data[6] = 0x00;
    data[13] = 0x00; data[14] = 0x00;

    RuuviData r = parseRuuviRAWv2(data, 24);
    TEST_ASSERT_TRUE(r.valid);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -2.1f, r.temperature);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_ruuvi_valid_max_values);
    RUN_TEST(test_ruuvi_valid_min_values);
    RUN_TEST(test_ruuvi_invalid_values);
    RUN_TEST(test_ruuvi_typical_indoor);
    RUN_TEST(test_ruuvi_wrong_format);
    RUN_TEST(test_ruuvi_too_short);
    RUN_TEST(test_ruuvi_negative_temperature);
    UNITY_END();
    return 0;
}
