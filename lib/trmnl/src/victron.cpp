#include "victron.h"
#include "trmnl_log.h"
#include <cstring>

// Victron BLE advertisement structure (after company ID 0x02E1):
//   [0]:     Record type
//   [1-2]:   Model ID (little-endian)
//   [3]:     Read-out type (0x01 = encrypted)
//   [4-5]:   Nonce/counter (little-endian, part of AES-CTR nonce)
//   [6]:     Encryption key byte 0 (used to verify correct key)
//   [7..N]:  Encrypted payload (typically 8-16 bytes)
//
// AES-CTR nonce construction (16 bytes):
//   [0-1]:   Nonce from bytes [4-5] of advertisement (little-endian)
//   [2-15]:  Zero

// ── AES-CTR decryption ──
// On ESP32: use mbedtls_aes_crypt_ctr
// On native/test: use a minimal software AES implementation

#if defined(ESP_PLATFORM)
#include <mbedtls/aes.h>

static bool aes_ctr_decrypt(const uint8_t* key, const uint8_t* nonce16,
                            const uint8_t* input, size_t len, uint8_t* output) {
    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    mbedtls_aes_setkey_enc(&ctx, key, 128);

    uint8_t nc[16];
    memcpy(nc, nonce16, 16);
    uint8_t stream[16] = {};
    size_t nc_off = 0;

    int ret = mbedtls_aes_crypt_ctr(&ctx, len, &nc_off, nc, stream, input, output);
    mbedtls_aes_free(&ctx);
    return (ret == 0);
}

#else
// Minimal AES-128 for native tests (not for production)
// Based on tiny-AES-c (public domain)

static const uint8_t sbox[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};

static const uint8_t rcon[11] = {0x00,0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1b,0x36};

static uint8_t xtime(uint8_t x) { return ((x<<1) ^ (((x>>7) & 1) * 0x1b)); }

static void key_expansion(const uint8_t* key, uint8_t* round_key) {
    memcpy(round_key, key, 16);
    for (int i = 4; i < 44; i++) {
        uint8_t temp[4];
        memcpy(temp, round_key + (i-1)*4, 4);
        if (i % 4 == 0) {
            uint8_t t = temp[0];
            temp[0] = sbox[temp[1]] ^ rcon[i/4];
            temp[1] = sbox[temp[2]];
            temp[2] = sbox[temp[3]];
            temp[3] = sbox[t];
        }
        for (int j = 0; j < 4; j++)
            round_key[i*4+j] = round_key[(i-4)*4+j] ^ temp[j];
    }
}

static void add_round_key(uint8_t* state, const uint8_t* rk, int round) {
    for (int i = 0; i < 16; i++) state[i] ^= rk[round*16+i];
}

static void sub_bytes(uint8_t* state) {
    for (int i = 0; i < 16; i++) state[i] = sbox[state[i]];
}

static void shift_rows(uint8_t* s) {
    uint8_t t;
    t=s[1]; s[1]=s[5]; s[5]=s[9]; s[9]=s[13]; s[13]=t;
    t=s[2]; s[2]=s[10]; s[10]=t; t=s[6]; s[6]=s[14]; s[14]=t;
    t=s[15]; s[15]=s[11]; s[11]=s[7]; s[7]=s[3]; s[3]=t;
}

static void mix_columns(uint8_t* s) {
    for (int i = 0; i < 4; i++) {
        uint8_t *c = s + i*4;
        uint8_t a0=c[0], a1=c[1], a2=c[2], a3=c[3];
        uint8_t h = a0^a1^a2^a3;
        c[0] ^= h ^ xtime(a0^a1);
        c[1] ^= h ^ xtime(a1^a2);
        c[2] ^= h ^ xtime(a2^a3);
        c[3] ^= h ^ xtime(a3^a0);
    }
}

static void aes_encrypt_block(const uint8_t* in, uint8_t* out, const uint8_t* round_key) {
    uint8_t state[16];
    memcpy(state, in, 16);
    add_round_key(state, round_key, 0);
    for (int r = 1; r < 10; r++) {
        sub_bytes(state); shift_rows(state); mix_columns(state);
        add_round_key(state, round_key, r);
    }
    sub_bytes(state); shift_rows(state);
    add_round_key(state, round_key, 10);
    memcpy(out, state, 16);
}

static bool aes_ctr_decrypt(const uint8_t* key, const uint8_t* nonce16,
                            const uint8_t* input, size_t len, uint8_t* output) {
    uint8_t round_key[176];
    key_expansion(key, round_key);

    uint8_t counter[16];
    memcpy(counter, nonce16, 16);

    for (size_t i = 0; i < len; i += 16) {
        uint8_t stream[16];
        aes_encrypt_block(counter, stream, round_key);

        size_t block_len = (len - i < 16) ? len - i : 16;
        for (size_t j = 0; j < block_len; j++) {
            output[i+j] = input[i+j] ^ stream[j];
        }

        // Increment counter (little-endian)
        for (int c = 0; c < 16; c++) {
            if (++counter[c] != 0) break;
        }
    }
    return true;
}
#endif // ESP_PLATFORM

// ── Advertisement parsing ──

VictronRecordType victronRecordType(const uint8_t* mfr_data, size_t mfr_len) {
    if (mfr_len < 1) return (VictronRecordType)0xFF;
    return (VictronRecordType)mfr_data[0];
}

bool victronDecrypt(const uint8_t* mfr_data, size_t mfr_len,
                    const uint8_t* key,
                    uint8_t* plain, size_t* plain_len) {
    // Minimum: 1 (type) + 2 (model) + 1 (readout type) + 2 (nonce) + 1 (key check) + 1 (data)
    if (mfr_len < 8) {
        Log_error("Victron: mfr data too short (%zu bytes)", mfr_len);
        return false;
    }
    if (mfr_data[3] != 0x01) {
        Log_verbose("Victron: readout type 0x%02X (not encrypted), skipping", mfr_data[3]);
        return false;
    }

    // Build nonce: bytes [4-5] as little-endian counter, rest zero
    uint8_t nonce[16] = {};
    nonce[0] = mfr_data[4];
    nonce[1] = mfr_data[5];

    const uint8_t* encrypted = mfr_data + 7;
    size_t enc_len = mfr_len - 7;

    if (!aes_ctr_decrypt(key, nonce, encrypted, enc_len, plain)) {
        Log_error("Victron: AES-CTR decryption failed");
        return false;
    }

    *plain_len = enc_len;
    Log_verbose("Victron: decrypted %zu bytes, record type=0x%02X", enc_len, plain[0]);
    return true;
}

// ── Record parsers ──

// Helper: read N bits from a byte array starting at bit offset (LSB-first)
static uint32_t read_bits(const uint8_t* data, int bit_offset, int n_bits) {
    uint32_t val = 0;
    for (int i = 0; i < n_bits; i++) {
        int byte_idx = (bit_offset + i) / 8;
        int bit_idx = (bit_offset + i) % 8;
        if (data[byte_idx] & (1 << bit_idx))
            val |= (1u << i);
    }
    return val;
}

// Helper: read little-endian unsigned
static uint16_t read_u16_le(const uint8_t* p) { return p[0] | (p[1] << 8); }
static uint32_t read_u32_le(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24); }

// Helper: read signed values with bit extraction
static int32_t sign_extend(uint32_t val, int bits) {
    uint32_t mask = 1u << (bits - 1);
    return (int32_t)((val ^ mask) - mask);
}

VictronSolar parseVictronSolar(const uint8_t* d, size_t len) {
    VictronSolar s = {};
    s.valid = false;
    if (len < 11) {
        Log_error("Victron Solar: payload too short (%zu bytes, need 11)", len);
        return s;
    }

    // SmartSolar decrypted payload (record type 0x01):
    //   [0]:   Charge state
    //   [1]:   Charger error
    //   [2-3]: Battery voltage (10mV units, unsigned)
    //   [4-5]: Battery current (10mA units, signed)
    //   [6-7]: Yield today (10mWh units)
    //   [8-9]: PV power (W, unsigned)
    //   [10]:  Load output state + extra bits

    s.charge_state = d[0];
    uint16_t raw_v = read_u16_le(d + 2);
    int16_t raw_i = (int16_t)read_u16_le(d + 4);
    uint16_t raw_pv = read_u16_le(d + 8);

    s.battery_voltage = (raw_v == 0x7FFF) ? NAN : raw_v * 0.01f;
    s.battery_current = (raw_i == 0x7FFF) ? NAN : raw_i * 0.1f;
    s.pv_power = (raw_pv == 0xFFFF) ? NAN : (float)raw_pv;
    s.valid = true;
    Log_info("Victron Solar: PV=%.0fW bat=%.2fV %.1fA state=%d",
             s.pv_power, s.battery_voltage, s.battery_current, s.charge_state);
    return s;
}

VictronShunt parseVictronShunt(const uint8_t* d, size_t len) {
    VictronShunt s = {};
    s.valid = false;
    if (len < 11) {
        Log_error("Victron Shunt: payload too short (%zu bytes, need 11)", len);
        return s;
    }

    // SmartShunt decrypted payload (record type 0x02):
    //   [0-1]:  TTG (time to go, minutes, unsigned)
    //   [2-3]:  Battery voltage (10mV, unsigned)
    //   [4]:    Alarm reason
    //   [5-6]:  Aux voltage (10mV, unsigned) — or temperature depending on config
    //   [7-8]:  Battery current (1mA, signed — actually stored in bits [7-9] as 22-bit signed)
    //          Current is a 22-bit signed value across bytes 7-9 (bits 0-21 of a 3-byte LE field)
    //   [9-10]: Consumed Ah / SoC packed
    //
    // Simplified parsing (may need adjustment per firmware version):

    uint16_t raw_v = read_u16_le(d + 2);
    uint16_t raw_aux = read_u16_le(d + 5);

    // Current: 22-bit signed value in bytes 7-9 (little-endian, bits 0-21)
    uint32_t raw_current = d[7] | (d[8] << 8) | ((d[9] & 0x3F) << 16);
    int32_t current_ma = sign_extend(raw_current, 22);

    // SoC: 10-bit value in bits [22-31] of bytes 9-10
    uint16_t raw_soc = ((d[9] >> 6) | (d[10] << 2)) & 0x3FF;

    s.battery_voltage = (raw_v == 0x7FFF) ? NAN : raw_v * 0.01f;
    s.battery_current = current_ma * 0.001f;
    s.aux_voltage = (raw_aux == 0x7FFF) ? NAN : raw_aux * 0.01f;
    s.soc = (raw_soc == 0x3FF) ? NAN : raw_soc * 0.1f;
    s.consumed_ah = 0;  // TODO: parse from remaining bytes if needed
    s.valid = true;
    Log_info("Victron Shunt: bat=%.2fV %.3fA aux=%.2fV SoC=%.1f%%",
             s.battery_voltage, s.battery_current, s.aux_voltage, s.soc);
    return s;
}

VictronVEBus parseVictronVEBus(const uint8_t* d, size_t len) {
    VictronVEBus s = {};
    s.valid = false;
    if (len < 13) {
        Log_error("Victron VE.Bus: payload too short (%zu bytes, need 13)", len);
        return s;
    }

    // VE.Bus (MultiPlus) decrypted payload — bitfield, LSB-first:
    //   [0:7]    device_state     u8
    //   [8:15]   ve_bus_error     u8
    //   [16:31]  battery_current  s16  (0.1A, positive=charging)
    //   [32:45]  battery_voltage  u14  (0.01V)
    //   [46:47]  active_ac_in     u2   (0=AC1, 1=AC2, 2=not connected)
    //   [48:66]  ac_in_power      s19  (W)
    //   [67:85]  ac_out_power     s19  (W)
    //   [86:87]  alarm            u2
    //   [88:94]  battery_temp     u7   (degC + 40 offset)
    //   [95:101] soc              u7   (%)

    s.device_state = d[0];

    uint32_t raw_i = read_bits(d, 16, 16);
    int16_t current_raw = (int16_t)(uint16_t)raw_i;

    uint32_t raw_v = read_bits(d, 32, 14);

    uint32_t raw_ac_in = read_bits(d, 48, 19);
    int32_t ac_in = sign_extend(raw_ac_in, 19);

    uint32_t raw_ac_out = read_bits(d, 67, 19);
    int32_t ac_out = sign_extend(raw_ac_out, 19);

    s.battery_voltage = (raw_v == 0x3FFF) ? NAN : raw_v * 0.01f;
    s.battery_current = (current_raw == 0x7FFF) ? NAN : current_raw * 0.1f;
    s.ac_in_power = (raw_ac_in == 0x7FFFF) ? 0 : ac_in;
    s.ac_out_power = (raw_ac_out == 0x7FFFF) ? 0 : ac_out;
    s.valid = true;
    Log_info("Victron VE.Bus: bat=%.2fV %.1fA ac_in=%dW ac_out=%dW state=%d",
             s.battery_voltage, s.battery_current, s.ac_in_power, s.ac_out_power, s.device_state);
    return s;
}
