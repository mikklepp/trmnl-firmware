#include <unity.h>
#include <victron.h>
#include <cstring>
#include <cmath>

void setUp(void) {}
void tearDown(void) {}

// Test key (16 bytes, arbitrary)
static const uint8_t TEST_KEY[16] = {
    0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,
    0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F,0x10
};

// Helper: encrypt a payload with AES-CTR using the same nonce construction
// so we can create test advertisements
static void encrypt_payload(const uint8_t* plain, size_t len,
                            uint8_t nonce_lo, uint8_t nonce_hi,
                            uint8_t* out) {
    // Reuse victronDecrypt by encrypting (CTR is symmetric)
    // Build a fake mfr_data with the nonce embedded
    uint8_t fake_mfr[64] = {};
    fake_mfr[0] = 0x01;  // record type (doesn't matter for encrypt)
    fake_mfr[1] = 0x00; fake_mfr[2] = 0x00;  // model
    fake_mfr[3] = 0x01;  // encrypted
    fake_mfr[4] = nonce_lo;
    fake_mfr[5] = nonce_hi;
    fake_mfr[6] = 0x00;  // key check byte
    memcpy(fake_mfr + 7, plain, len);

    // Decrypt (= encrypt for CTR) to get the ciphertext
    uint8_t decrypted[32];
    size_t dec_len;
    victronDecrypt(fake_mfr, 7 + len, TEST_KEY, decrypted, &dec_len);
    // The "decrypted" output is actually the encrypted version of plain
    // because we passed plain as if it were ciphertext.
    // Wait — that's wrong. CTR: decrypt(encrypt(plain)) = plain.
    // We need: encrypt(plain) = CTR_keystream XOR plain.
    // victronDecrypt does: output = ciphertext XOR keystream.
    // If we pass plain as ciphertext: output = plain XOR keystream = ciphertext.
    memcpy(out, decrypted, len);
}

// Build a complete manufacturer data blob for testing
static size_t build_mfr_data(uint8_t* buf, uint8_t record_type, uint16_t model,
                              const uint8_t* encrypted_payload, size_t payload_len) {
    buf[0] = record_type;
    buf[1] = model & 0xFF;
    buf[2] = (model >> 8) & 0xFF;
    buf[3] = 0x01;  // encrypted
    buf[4] = 0x42;  // nonce lo
    buf[5] = 0x00;  // nonce hi
    buf[6] = 0x00;  // key check
    memcpy(buf + 7, encrypted_payload, payload_len);
    return 7 + payload_len;
}

// ── Decrypt tests ──

void test_decrypt_roundtrip(void) {
    // Encrypt then decrypt should give back original
    uint8_t plain[16] = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03, 0x04,
                         0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C};
    uint8_t encrypted[16];
    encrypt_payload(plain, 16, 0x42, 0x00, encrypted);

    uint8_t mfr[32];
    size_t mfr_len = build_mfr_data(mfr, 0x01, 0xA389, encrypted, 16);

    uint8_t decrypted[16];
    size_t dec_len;
    bool ok = victronDecrypt(mfr, mfr_len, TEST_KEY, decrypted, &dec_len);

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_INT(16, (int)dec_len);
    TEST_ASSERT_EQUAL_MEMORY(plain, decrypted, 16);
}

void test_decrypt_too_short(void) {
    uint8_t mfr[4] = {0x01, 0x00, 0x00, 0x01};
    uint8_t plain[16];
    size_t len;
    TEST_ASSERT_FALSE(victronDecrypt(mfr, 4, TEST_KEY, plain, &len));
}

void test_decrypt_not_encrypted(void) {
    uint8_t mfr[16] = {};
    mfr[3] = 0x00;  // not encrypted flag
    uint8_t plain[16];
    size_t len;
    TEST_ASSERT_FALSE(victronDecrypt(mfr, 16, TEST_KEY, plain, &len));
}

// ── SmartSolar parser tests ──

void test_parse_solar_basic(void) {
    // Construct a known plaintext SmartSolar payload
    uint8_t payload[11] = {};
    payload[0] = 5;                       // charge state: float
    payload[1] = 0;                       // charger error: none
    payload[2] = 0xE8; payload[3] = 0x03; // voltage: 1000 = 10.00V
    payload[4] = 0x64; payload[5] = 0x00; // current: 100 = 10.0A
    payload[6] = 0x00; payload[7] = 0x00; // yield today
    payload[8] = 0x91; payload[9] = 0x00; // PV power: 145W
    payload[10] = 0x00;                   // load output

    VictronSolar s = parseVictronSolar(payload, 11);
    TEST_ASSERT_TRUE(s.valid);
    TEST_ASSERT_EQUAL_INT(5, s.charge_state);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f, s.battery_voltage);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 10.0f, s.battery_current);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 145.0f, s.pv_power);
}

void test_parse_solar_invalid_voltage(void) {
    uint8_t payload[11] = {};
    payload[2] = 0xFF; payload[3] = 0x7F; // 0x7FFF = invalid
    payload[8] = 0xFF; payload[9] = 0xFF; // 0xFFFF = invalid PV

    VictronSolar s = parseVictronSolar(payload, 11);
    TEST_ASSERT_TRUE(s.valid);
    TEST_ASSERT_TRUE(isnan(s.battery_voltage));
    TEST_ASSERT_TRUE(isnan(s.pv_power));
}

void test_parse_solar_too_short(void) {
    uint8_t payload[5] = {};
    VictronSolar s = parseVictronSolar(payload, 5);
    TEST_ASSERT_FALSE(s.valid);
}

// ── SmartShunt parser tests ──

void test_parse_shunt_basic(void) {
    uint8_t payload[11] = {};
    payload[0] = 0x00; payload[1] = 0x00; // TTG
    payload[2] = 0xE8; payload[3] = 0x03; // voltage: 1000 = 10.00V
    payload[4] = 0x00;                     // alarm
    payload[5] = 0x00; payload[6] = 0x05; // aux voltage: 1280 = 12.80V
    // Current: -5000 mA = -5.0A as 22-bit signed
    // -5000 in 22-bit = 0x3FEC78 → bytes: 0x78, 0xEC, 0x3F (but only 6 bits of byte 9)
    int32_t current_val = -5000;
    uint32_t current_u22 = (uint32_t)(current_val & 0x3FFFFF);
    payload[7] = current_u22 & 0xFF;
    payload[8] = (current_u22 >> 8) & 0xFF;
    // SoC: 980 (= 98.0%) as 10-bit in bits [22-31]
    uint16_t soc_raw = 980;
    payload[9] = ((current_u22 >> 16) & 0x3F) | ((soc_raw & 0x03) << 6);
    payload[10] = (soc_raw >> 2) & 0xFF;

    VictronShunt s = parseVictronShunt(payload, 11);
    TEST_ASSERT_TRUE(s.valid);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f, s.battery_voltage);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, -5.0f, s.battery_current);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 12.80f, s.aux_voltage);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 98.0f, s.soc);
}

void test_parse_shunt_positive_current(void) {
    uint8_t payload[11] = {};
    payload[2] = 0xE8; payload[3] = 0x03;
    payload[5] = 0x00; payload[6] = 0x05;
    // Current: +3000 mA
    uint32_t current_u22 = 3000 & 0x3FFFFF;
    payload[7] = current_u22 & 0xFF;
    payload[8] = (current_u22 >> 8) & 0xFF;
    payload[9] = (current_u22 >> 16) & 0x3F;
    payload[10] = 0x00;

    VictronShunt s = parseVictronShunt(payload, 11);
    TEST_ASSERT_TRUE(s.valid);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 3.0f, s.battery_current);
}

void test_parse_shunt_soc_unavailable(void) {
    uint8_t payload[11] = {};
    payload[2] = 0xE8; payload[3] = 0x03;
    payload[5] = 0x00; payload[6] = 0x05;
    // SoC: 0x3FF = unavailable
    payload[9] = 0xC0;  // bits 6-7 = low 2 bits of 0x3FF
    payload[10] = 0xFF; // bits 0-7 = high 8 bits of 0x3FF

    VictronShunt s = parseVictronShunt(payload, 11);
    TEST_ASSERT_TRUE(s.valid);
    TEST_ASSERT_TRUE(isnan(s.soc));
}

// ── VE.Bus (MultiPlus) parser tests ──

// Helper: set bits in a byte array (LSB-first, matching the parser)
static void set_bits(uint8_t* data, int bit_offset, int n_bits, uint32_t val) {
    for (int i = 0; i < n_bits; i++) {
        int byte_idx = (bit_offset + i) / 8;
        int bit_idx = (bit_offset + i) % 8;
        if (val & (1u << i))
            data[byte_idx] |= (1 << bit_idx);
        else
            data[byte_idx] &= ~(1 << bit_idx);
    }
}

void test_parse_vebus_charging(void) {
    // MultiPlus charging from shore: 28.4V, 15.2A, AC in 500W, AC out 200W
    uint8_t payload[13] = {};
    set_bits(payload, 0, 8, 0x03);      // device_state: bulk charging
    set_bits(payload, 8, 8, 0x00);      // error: none
    set_bits(payload, 16, 16, 152);     // battery_current: 152 = 15.2A (charging)
    set_bits(payload, 32, 14, 2840);    // battery_voltage: 2840 = 28.40V
    set_bits(payload, 46, 2, 0);        // active_ac_in: AC_IN_1
    set_bits(payload, 48, 19, 500);     // ac_in_power: 500W
    set_bits(payload, 67, 19, 200);     // ac_out_power: 200W

    VictronVEBus s = parseVictronVEBus(payload, 13);
    TEST_ASSERT_TRUE(s.valid);
    TEST_ASSERT_EQUAL_INT(0x03, s.device_state);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 28.40f, s.battery_voltage);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 15.2f, s.battery_current);
    TEST_ASSERT_EQUAL_INT(500, s.ac_in_power);
    TEST_ASSERT_EQUAL_INT(200, s.ac_out_power);
}

void test_parse_vebus_inverting(void) {
    // MultiPlus inverting: 24.8V, -12.5A (discharging), AC out 300W, no AC in
    uint8_t payload[13] = {};
    set_bits(payload, 0, 8, 0x09);      // device_state: inverting
    set_bits(payload, 16, 16, (uint16_t)(int16_t)(-125)); // -12.5A
    set_bits(payload, 32, 14, 2480);    // 24.80V
    set_bits(payload, 46, 2, 2);        // active_ac_in: not connected
    set_bits(payload, 48, 19, 0);       // ac_in: 0W
    set_bits(payload, 67, 19, 300);     // ac_out: 300W

    VictronVEBus s = parseVictronVEBus(payload, 13);
    TEST_ASSERT_TRUE(s.valid);
    TEST_ASSERT_EQUAL_INT(0x09, s.device_state);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 24.80f, s.battery_voltage);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, -12.5f, s.battery_current);
    TEST_ASSERT_EQUAL_INT(0, s.ac_in_power);
    TEST_ASSERT_EQUAL_INT(300, s.ac_out_power);
}

void test_parse_vebus_unavailable(void) {
    uint8_t payload[13] = {};
    set_bits(payload, 16, 16, 0x7FFF);  // current: unavailable
    set_bits(payload, 32, 14, 0x3FFF);  // voltage: unavailable
    set_bits(payload, 48, 19, 0x7FFFF); // ac_in: unavailable
    set_bits(payload, 67, 19, 0x7FFFF); // ac_out: unavailable

    VictronVEBus s = parseVictronVEBus(payload, 13);
    TEST_ASSERT_TRUE(s.valid);
    TEST_ASSERT_TRUE(isnan(s.battery_voltage));
    TEST_ASSERT_TRUE(isnan(s.battery_current));
    TEST_ASSERT_EQUAL_INT(0, s.ac_in_power);
    TEST_ASSERT_EQUAL_INT(0, s.ac_out_power);
}

void test_parse_vebus_too_short(void) {
    uint8_t payload[8] = {};
    VictronVEBus s = parseVictronVEBus(payload, 8);
    TEST_ASSERT_FALSE(s.valid);
}

void test_record_type(void) {
    uint8_t mfr[] = {0x02, 0x00, 0x00};
    TEST_ASSERT_EQUAL_INT(VICTRON_BATTERY_MONITOR, victronRecordType(mfr, 3));
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_decrypt_roundtrip);
    RUN_TEST(test_decrypt_too_short);
    RUN_TEST(test_decrypt_not_encrypted);
    RUN_TEST(test_parse_solar_basic);
    RUN_TEST(test_parse_solar_invalid_voltage);
    RUN_TEST(test_parse_solar_too_short);
    RUN_TEST(test_parse_shunt_basic);
    RUN_TEST(test_parse_shunt_positive_current);
    RUN_TEST(test_parse_shunt_soc_unavailable);
    RUN_TEST(test_parse_vebus_charging);
    RUN_TEST(test_parse_vebus_inverting);
    RUN_TEST(test_parse_vebus_unavailable);
    RUN_TEST(test_parse_vebus_too_short);
    RUN_TEST(test_record_type);
    UNITY_END();
    return 0;
}
