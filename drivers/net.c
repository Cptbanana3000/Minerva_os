#include <stdint.h>
#include "net.h"
#include "e1000.h"
#include "libc.h"
#include "sha256.h"
#include "p256.h"

#define ETHERTYPE_ARP 0x0806u
#define ETHERTYPE_IPV4 0x0800u
#define ARP_HTYPE_ETHERNET 0x0001u
#define ARP_OP_REQUEST 0x0001u
#define ARP_OP_REPLY 0x0002u
#define IP_PROTO_TCP 6u
#define IP_PROTO_UDP 17u
#define DNS_PORT 53u
#define DNS_SOURCE_PORT 49152u
#define DNS_QUERY_ID 0x4D4Eu
#define TCP_HTTP_PORT 80u
#define TCP_TLS_PORT 443u
#define TCP_DNS_PORT 53u
#define TCP_SOURCE_PORT_BASE 49153u
#define TCP_SEQ_BASE 0x4D4E1000u
#define TCP_FLAG_FIN 0x01u
#define TCP_FLAG_SYN 0x02u
#define TCP_FLAG_RST 0x04u
#define TCP_FLAG_PSH 0x08u
#define TCP_FLAG_ACK 0x10u
#define TCP_OPT_MSS 0x02u
#define TCP_OPT_SACK_PERMITTED 0x04u
#define TCP_OPT_NOP 0x01u
#define TCP_OPT_WINDOW_SCALE 0x03u
#define NET_TLS_CERT_CAPTURE_SIZE 8192u
#define NET_TLS_TRANSCRIPT_SIZE 10000u
#define NET_TLS_MASTER_SECRET_SIZE 48u
#define NET_TLS_KEY_BLOCK_SIZE 40u

typedef struct __attribute__((packed)) {
    uint8_t dst[6];
    uint8_t src[6];
    uint8_t ethertype[2];
} ethernet_header_t;

typedef struct __attribute__((packed)) {
    uint8_t htype[2];
    uint8_t ptype[2];
    uint8_t hlen;
    uint8_t plen;
    uint8_t oper[2];
    uint8_t sha[6];
    uint8_t spa[4];
    uint8_t tha[6];
    uint8_t tpa[4];
} arp_packet_t;

static net_info_t g_net;
static uint8_t rx_frame[1536];
static uint8_t tls_certificate_body[NET_TLS_CERT_CAPTURE_SIZE];
static uint8_t tls_kex_params[NET_TLS_KEX_PARAMS_SIZE];
static uint8_t tls_transcript[NET_TLS_TRANSCRIPT_SIZE];
static uint8_t tls_shared_secret[NET_TLS_P256_SIZE];
static uint8_t tls_master_secret[NET_TLS_MASTER_SECRET_SIZE];
static uint8_t tls_key_block[NET_TLS_KEY_BLOCK_SIZE];
static uint8_t tls_client_finished[NET_TLS_FINISHED_SIZE];
static uint8_t tls_app_plain[NET_HTTP_CAPTURE_SIZE];
static uint8_t tls_app_record[NET_HTTP_CAPTURE_SIZE];
static uint8_t tls_transcript_certificate_added;
static uint16_t tls_app_pending_len;
static uint16_t tls_app_pending_rx;
static const uint8_t tls_client_scalar[NET_TLS_P256_SIZE] = {
    0x11, 0x42, 0x4D, 0x69, 0x6E, 0x65, 0x72, 0x76,
    0x61, 0x54, 0x4C, 0x53, 0x2D, 0x45, 0x43, 0x44,
    0x48, 0x45, 0x2D, 0x50, 0x32, 0x35, 0x36, 0x2D,
    0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x31
};

static void put16be(uint8_t *dst, uint16_t value) {
    dst[0] = (uint8_t)(value >> 8);
    dst[1] = (uint8_t)(value & 0xFFu);
}

static uint16_t get16be(const uint8_t *src) {
    return (uint16_t)(((uint16_t)src[0] << 8) | src[1]);
}

static void put32be(uint8_t *dst, uint32_t value) {
    dst[0] = (uint8_t)(value >> 24);
    dst[1] = (uint8_t)((value >> 16) & 0xFFu);
    dst[2] = (uint8_t)((value >> 8) & 0xFFu);
    dst[3] = (uint8_t)(value & 0xFFu);
}

static uint32_t get32be(const uint8_t *src) {
    return ((uint32_t)src[0] << 24) |
           ((uint32_t)src[1] << 16) |
           ((uint32_t)src[2] << 8) |
           src[3];
}

static int net_tls_parse_der_sequence_len(const uint8_t *der,
                                          uint32_t available,
                                          uint32_t *content_len,
                                          uint8_t *header_len);
static int net_tls_parse_der_len_at(const uint8_t *src,
                                    uint32_t available,
                                    uint32_t *content_len,
                                    uint8_t *header_len);
static void put24be(uint8_t *dst, uint32_t value);
static int net_tls_certificate_complete(void);

static void net_tls_transcript_reset(void) {
    memset(tls_transcript, 0, sizeof(tls_transcript));
    g_net.tls_transcript_len = 0;
    g_net.tls_transcript_overflow = 0;
    g_net.tls_handshake_hash32 = 0;
    g_net.tls_client_finished_valid = 0;
    g_net.tls_client_finished0 = 0;
    g_net.tls_client_finished1 = 0;
    g_net.tls_client_finished2 = 0;
    g_net.tls_ccs_tx = 0;
    g_net.tls_finished_tx = 0;
    g_net.tls_finished_acked = 0;
    g_net.tls_finished_record_len = 0;
    g_net.tls_finished_tag32 = 0;
    g_net.tls_server_ccs_rx = 0;
    g_net.tls_server_finished_rx = 0;
    g_net.tls_server_finished_decrypt = 0;
    g_net.tls_server_finished_verify = 0;
    g_net.tls_server_finished0 = 0;
    g_net.tls_server_finished1 = 0;
    g_net.tls_server_finished2 = 0;
    g_net.tls_server_finished_tag32 = 0;
    g_net.tls_app_tx = 0;
    g_net.tls_app_acked = 0;
    g_net.tls_app_rx = 0;
    g_net.tls_app_decrypt = 0;
    g_net.tls_app_record_len = 0;
    g_net.tls_app_plain_len = 0;
    g_net.tls_app_response_len = 0;
    g_net.tls_app_pending_len = 0;
    g_net.tls_app_pending_rx = 0;
    g_net.tls_app_tag32 = 0;
    g_net.tls_app_response_tag32 = 0;
    memset(tls_app_plain, 0, sizeof(tls_app_plain));
    memset(tls_app_record, 0, sizeof(tls_app_record));
    tls_app_pending_len = 0;
    tls_app_pending_rx = 0;
    memset(tls_client_finished, 0, sizeof(tls_client_finished));
    tls_transcript_certificate_added = 0;
}

static void net_tls_transcript_append(const uint8_t *data, uint32_t len) {
    if (!data || !len) return;
    if (g_net.tls_transcript_len >= NET_TLS_TRANSCRIPT_SIZE ||
        len > NET_TLS_TRANSCRIPT_SIZE - g_net.tls_transcript_len) {
        g_net.tls_transcript_overflow = 1;
        return;
    }
    memcpy(tls_transcript + g_net.tls_transcript_len, data, len);
    g_net.tls_transcript_len = (uint16_t)(g_net.tls_transcript_len + len);
}

static void net_tls_transcript_append_message(uint8_t type,
                                              const uint8_t *body,
                                              uint32_t len) {
    uint8_t header[4];
    header[0] = type;
    put24be(header + 1, len);
    net_tls_transcript_append(header, sizeof(header));
    net_tls_transcript_append(body, len);
}

static void net_tls_transcript_append_certificate(void) {
    if (tls_transcript_certificate_added) return;
    if (!net_tls_certificate_complete()) return;
    if (g_net.tls_certificate_len > NET_TLS_CERT_CAPTURE_SIZE) return;
    net_tls_transcript_append_message(0x0B,
                                      tls_certificate_body,
                                      g_net.tls_certificate_len);
    tls_transcript_certificate_added = 1;
}

static int net_tls_verify_ecdsa_p256(const uint8_t pub_x[NET_TLS_P256_SIZE],
                                     const uint8_t pub_y[NET_TLS_P256_SIZE],
                                     const uint8_t sig_r[NET_TLS_P256_SIZE],
                                     const uint8_t sig_s[NET_TLS_P256_SIZE],
                                     const uint8_t hash[NET_TLS_P256_SIZE],
                                     uint32_t *v32) {
    p256_fe_t r;
    p256_fe_t s;
    p256_fe_t z;
    p256_fe_t w;
    p256_fe_t u1;
    p256_fe_t u2;
    p256_fe_t v;
    p256_point_t g;
    p256_point_t q;
    p256_point_t u1g;
    p256_point_t u2q;
    p256_point_t sum;
    uint8_t u1_be[NET_TLS_P256_SIZE];
    uint8_t u2_be[NET_TLS_P256_SIZE];

    if (v32) *v32 = 0;
    if (!p256_scalar_valid_be32(sig_r)) return 0;
    if (!p256_scalar_valid_be32(sig_s)) return 0;
    if (!p256_point_from_be32(&q, pub_x, pub_y)) return 0;

    p256_scalar_from_be32(&r, sig_r);
    p256_scalar_from_be32(&s, sig_s);
    p256_scalar_from_be32(&z, hash);
    p256_scalar_inv(&w, &s);
    p256_scalar_mul(&u1, &z, &w);
    p256_scalar_mul(&u2, &r, &w);
    p256_fe_to_be32(u1_be, &u1);
    p256_fe_to_be32(u2_be, &u2);

    p256_base_point(&g);
    p256_point_mul_be32_projective(&u1g, &g, u1_be);
    p256_point_mul_be32_projective(&u2q, &q, u2_be);
    p256_point_add(&sum, &u1g, &u2q);
    if (sum.infinity) return 0;

    p256_scalar_from_fe(&v, &sum.x);
    if (v32) *v32 = p256_fe_prefix32(&v);
    return p256_fe_equal_public(&v, &r);
}

static void net_hmac_sha256(const uint8_t *key,
                            uint32_t key_len,
                            const uint8_t *data,
                            uint32_t data_len,
                            uint8_t out[32]) {
    uint8_t k0[64];
    uint8_t inner[64 + 160];
    uint8_t outer[64 + 32];
    uint8_t inner_hash[32];

    memset(k0, 0, sizeof(k0));
    if (key_len > 64) {
        sha256_hash(key, key_len, k0);
    } else if (key_len) {
        memcpy(k0, key, key_len);
    }

    for (uint8_t i = 0; i < 64; i++) {
        inner[i] = (uint8_t)(k0[i] ^ 0x36u);
        outer[i] = (uint8_t)(k0[i] ^ 0x5Cu);
    }
    if (data_len > 160) data_len = 160;
    memcpy(inner + 64, data, data_len);
    sha256_hash(inner, 64u + data_len, inner_hash);

    memcpy(outer + 64, inner_hash, sizeof(inner_hash));
    sha256_hash(outer, sizeof(outer), out);
}

static void net_tls_prf_sha256(const uint8_t *secret,
                               uint32_t secret_len,
                               const char *label,
                               const uint8_t *seed,
                               uint32_t seed_len,
                               uint8_t *out,
                               uint32_t out_len) {
    uint8_t label_seed[128];
    uint8_t a[32];
    uint8_t block_data[32 + 128];
    uint8_t block[32];
    uint32_t label_len = strlen(label);
    uint32_t label_seed_len = label_len + seed_len;
    uint32_t produced = 0;

    if (label_seed_len > sizeof(label_seed)) return;
    memcpy(label_seed, label, label_len);
    memcpy(label_seed + label_len, seed, seed_len);

    net_hmac_sha256(secret, secret_len, label_seed, label_seed_len, a);
    while (produced < out_len) {
        memcpy(block_data, a, sizeof(a));
        memcpy(block_data + sizeof(a), label_seed, label_seed_len);
        net_hmac_sha256(secret,
                        secret_len,
                        block_data,
                        sizeof(a) + label_seed_len,
                        block);
        uint32_t copy = out_len - produced;
        if (copy > sizeof(block)) copy = sizeof(block);
        memcpy(out + produced, block, copy);
        produced += copy;
        net_hmac_sha256(secret, secret_len, a, sizeof(a), a);
    }
}

static const uint8_t aes_sbox[256] = {
    0x63,0x7C,0x77,0x7B,0xF2,0x6B,0x6F,0xC5,0x30,0x01,0x67,0x2B,0xFE,0xD7,0xAB,0x76,
    0xCA,0x82,0xC9,0x7D,0xFA,0x59,0x47,0xF0,0xAD,0xD4,0xA2,0xAF,0x9C,0xA4,0x72,0xC0,
    0xB7,0xFD,0x93,0x26,0x36,0x3F,0xF7,0xCC,0x34,0xA5,0xE5,0xF1,0x71,0xD8,0x31,0x15,
    0x04,0xC7,0x23,0xC3,0x18,0x96,0x05,0x9A,0x07,0x12,0x80,0xE2,0xEB,0x27,0xB2,0x75,
    0x09,0x83,0x2C,0x1A,0x1B,0x6E,0x5A,0xA0,0x52,0x3B,0xD6,0xB3,0x29,0xE3,0x2F,0x84,
    0x53,0xD1,0x00,0xED,0x20,0xFC,0xB1,0x5B,0x6A,0xCB,0xBE,0x39,0x4A,0x4C,0x58,0xCF,
    0xD0,0xEF,0xAA,0xFB,0x43,0x4D,0x33,0x85,0x45,0xF9,0x02,0x7F,0x50,0x3C,0x9F,0xA8,
    0x51,0xA3,0x40,0x8F,0x92,0x9D,0x38,0xF5,0xBC,0xB6,0xDA,0x21,0x10,0xFF,0xF3,0xD2,
    0xCD,0x0C,0x13,0xEC,0x5F,0x97,0x44,0x17,0xC4,0xA7,0x7E,0x3D,0x64,0x5D,0x19,0x73,
    0x60,0x81,0x4F,0xDC,0x22,0x2A,0x90,0x88,0x46,0xEE,0xB8,0x14,0xDE,0x5E,0x0B,0xDB,
    0xE0,0x32,0x3A,0x0A,0x49,0x06,0x24,0x5C,0xC2,0xD3,0xAC,0x62,0x91,0x95,0xE4,0x79,
    0xE7,0xC8,0x37,0x6D,0x8D,0xD5,0x4E,0xA9,0x6C,0x56,0xF4,0xEA,0x65,0x7A,0xAE,0x08,
    0xBA,0x78,0x25,0x2E,0x1C,0xA6,0xB4,0xC6,0xE8,0xDD,0x74,0x1F,0x4B,0xBD,0x8B,0x8A,
    0x70,0x3E,0xB5,0x66,0x48,0x03,0xF6,0x0E,0x61,0x35,0x57,0xB9,0x86,0xC1,0x1D,0x9E,
    0xE1,0xF8,0x98,0x11,0x69,0xD9,0x8E,0x94,0x9B,0x1E,0x87,0xE9,0xCE,0x55,0x28,0xDF,
    0x8C,0xA1,0x89,0x0D,0xBF,0xE6,0x42,0x68,0x41,0x99,0x2D,0x0F,0xB0,0x54,0xBB,0x16
};

static uint8_t aes_xtime(uint8_t x) {
    return (uint8_t)((x << 1) ^ ((x & 0x80u) ? 0x1Bu : 0));
}

static void aes_key_expand(const uint8_t key[16], uint8_t round_keys[176]) {
    static const uint8_t rcon[10] = {0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1B,0x36};
    memcpy(round_keys, key, 16);
    uint8_t bytes = 16;
    uint8_t rcon_i = 0;
    uint8_t tmp[4];
    while (bytes < 176) {
        for (uint8_t i = 0; i < 4; i++) tmp[i] = round_keys[bytes - 4 + i];
        if ((bytes & 15u) == 0) {
            uint8_t t = tmp[0];
            tmp[0] = (uint8_t)(aes_sbox[tmp[1]] ^ rcon[rcon_i++]);
            tmp[1] = aes_sbox[tmp[2]];
            tmp[2] = aes_sbox[tmp[3]];
            tmp[3] = aes_sbox[t];
        }
        for (uint8_t i = 0; i < 4; i++) {
            round_keys[bytes] = (uint8_t)(round_keys[bytes - 16] ^ tmp[i]);
            bytes++;
        }
    }
}

static void aes_add_round_key(uint8_t state[16], const uint8_t *round_key) {
    for (uint8_t i = 0; i < 16; i++) state[i] ^= round_key[i];
}

static void aes_sub_bytes(uint8_t state[16]) {
    for (uint8_t i = 0; i < 16; i++) state[i] = aes_sbox[state[i]];
}

static void aes_shift_rows(uint8_t s[16]) {
    uint8_t t;
    t = s[1]; s[1] = s[5]; s[5] = s[9]; s[9] = s[13]; s[13] = t;
    t = s[2]; s[2] = s[10]; s[10] = t; t = s[6]; s[6] = s[14]; s[14] = t;
    t = s[15]; s[15] = s[11]; s[11] = s[7]; s[7] = s[3]; s[3] = t;
}

static void aes_mix_columns(uint8_t s[16]) {
    for (uint8_t c = 0; c < 4; c++) {
        uint8_t *a = s + c * 4u;
        uint8_t t = (uint8_t)(a[0] ^ a[1] ^ a[2] ^ a[3]);
        uint8_t u = a[0];
        a[0] ^= t ^ aes_xtime((uint8_t)(a[0] ^ a[1]));
        a[1] ^= t ^ aes_xtime((uint8_t)(a[1] ^ a[2]));
        a[2] ^= t ^ aes_xtime((uint8_t)(a[2] ^ a[3]));
        a[3] ^= t ^ aes_xtime((uint8_t)(a[3] ^ u));
    }
}

static void aes128_encrypt_block(const uint8_t key[16],
                                 const uint8_t in[16],
                                 uint8_t out[16]) {
    uint8_t round_keys[176];
    uint8_t state[16];
    aes_key_expand(key, round_keys);
    memcpy(state, in, 16);
    aes_add_round_key(state, round_keys);
    for (uint8_t round = 1; round < 10; round++) {
        aes_sub_bytes(state);
        aes_shift_rows(state);
        aes_mix_columns(state);
        aes_add_round_key(state, round_keys + round * 16u);
    }
    aes_sub_bytes(state);
    aes_shift_rows(state);
    aes_add_round_key(state, round_keys + 160);
    memcpy(out, state, 16);
}

static void gcm_shift_right(uint8_t v[16]) {
    uint8_t carry = 0;
    for (uint8_t i = 0; i < 16; i++) {
        uint8_t next = (uint8_t)(v[i] & 1u);
        v[i] = (uint8_t)((v[i] >> 1) | (carry ? 0x80u : 0));
        carry = next;
    }
}

static void gcm_mul(uint8_t x[16], const uint8_t h[16]) {
    uint8_t z[16];
    uint8_t v[16];
    memset(z, 0, sizeof(z));
    memcpy(v, h, 16);
    for (uint8_t i = 0; i < 128; i++) {
        if ((x[i >> 3] >> (7 - (i & 7))) & 1u) {
            for (uint8_t j = 0; j < 16; j++) z[j] ^= v[j];
        }
        uint8_t lsb = (uint8_t)(v[15] & 1u);
        gcm_shift_right(v);
        if (lsb) v[0] ^= 0xE1u;
    }
    memcpy(x, z, 16);
}

static void ghash_block(uint8_t y[16], const uint8_t h[16], const uint8_t block[16]) {
    for (uint8_t i = 0; i < 16; i++) y[i] ^= block[i];
    gcm_mul(y, h);
}

static void aes128_gcm_encrypt(const uint8_t key[16],
                               const uint8_t nonce[12],
                               const uint8_t aad[13],
                               const uint8_t *plain,
                               uint16_t plain_len,
                               uint8_t *cipher,
                               uint8_t tag[16]) {
    uint8_t zero[16];
    uint8_t h[16];
    uint8_t y[16];
    uint8_t block[16];
    uint8_t ctr[16];
    uint8_t stream[16];
    memset(zero, 0, sizeof(zero));
    aes128_encrypt_block(key, zero, h);
    memset(y, 0, sizeof(y));
    memset(block, 0, sizeof(block));
    memcpy(block, aad, 13);
    ghash_block(y, h, block);

    memcpy(ctr, nonce, 12);
    ctr[12] = 0; ctr[13] = 0; ctr[14] = 0; ctr[15] = 2;
    for (uint16_t off = 0; off < plain_len; off += 16) {
        uint16_t chunk = (uint16_t)(plain_len - off);
        if (chunk > 16) chunk = 16;
        aes128_encrypt_block(key, ctr, stream);
        memset(block, 0, sizeof(block));
        for (uint8_t i = 0; i < chunk; i++) {
            cipher[off + i] = (uint8_t)(plain[off + i] ^ stream[i]);
            block[i] = cipher[off + i];
        }
        ghash_block(y, h, block);
        for (int i = 15; i >= 12; i--) {
            ctr[i]++;
            if (ctr[i]) break;
        }
    }

    memset(block, 0, sizeof(block));
    put32be(block + 4, 13u * 8u);
    put32be(block + 12, (uint32_t)plain_len * 8u);
    ghash_block(y, h, block);

    memcpy(ctr, nonce, 12);
    ctr[12] = 0; ctr[13] = 0; ctr[14] = 0; ctr[15] = 1;
    aes128_encrypt_block(key, ctr, stream);
    for (uint8_t i = 0; i < 16; i++) tag[i] = (uint8_t)(stream[i] ^ y[i]);
}

static int aes128_gcm_decrypt(const uint8_t key[16],
                              const uint8_t nonce[12],
                              const uint8_t aad[13],
                              const uint8_t *cipher,
                              uint16_t cipher_len,
                              const uint8_t tag[16],
                              uint8_t *plain) {
    uint8_t calc_tag[16];
    uint8_t zero[16];
    uint8_t h[16];
    uint8_t y[16];
    uint8_t block[16];
    uint8_t ctr[16];
    uint8_t stream[16];

    memset(zero, 0, sizeof(zero));
    aes128_encrypt_block(key, zero, h);
    memset(y, 0, sizeof(y));
    memset(block, 0, sizeof(block));
    memcpy(block, aad, 13);
    ghash_block(y, h, block);

    for (uint16_t off = 0; off < cipher_len; off += 16) {
        uint16_t chunk = (uint16_t)(cipher_len - off);
        if (chunk > 16) chunk = 16;
        memset(block, 0, sizeof(block));
        memcpy(block, cipher + off, chunk);
        ghash_block(y, h, block);
    }

    memset(block, 0, sizeof(block));
    put32be(block + 4, 13u * 8u);
    put32be(block + 12, (uint32_t)cipher_len * 8u);
    ghash_block(y, h, block);

    memcpy(ctr, nonce, 12);
    ctr[12] = 0; ctr[13] = 0; ctr[14] = 0; ctr[15] = 1;
    aes128_encrypt_block(key, ctr, stream);
    for (uint8_t i = 0; i < 16; i++) calc_tag[i] = (uint8_t)(stream[i] ^ y[i]);

    uint8_t diff = 0;
    for (uint8_t i = 0; i < 16; i++) diff |= (uint8_t)(calc_tag[i] ^ tag[i]);
    if (diff) return 0;

    memcpy(ctr, nonce, 12);
    ctr[12] = 0; ctr[13] = 0; ctr[14] = 0; ctr[15] = 2;
    for (uint16_t off = 0; off < cipher_len; off += 16) {
        uint16_t chunk = (uint16_t)(cipher_len - off);
        if (chunk > 16) chunk = 16;
        aes128_encrypt_block(key, ctr, stream);
        for (uint8_t i = 0; i < chunk; i++) {
            plain[off + i] = (uint8_t)(cipher[off + i] ^ stream[i]);
        }
        for (int i = 15; i >= 12; i--) {
            ctr[i]++;
            if (ctr[i]) break;
        }
    }

    return 1;
}

static void net_tls_compute_ecdsa_scalars(void) {
    p256_fe_t r;
    p256_fe_t s;
    p256_fe_t z;
    p256_fe_t w;
    p256_fe_t u1;
    p256_fe_t u2;
    p256_point_t q;

    g_net.tls_x509_ecdsa_scalar_inputs = 0;
    g_net.tls_x509_ecdsa_pubkey_valid = 0;
    g_net.tls_x509_ecdsa_point_done = 0;
    g_net.tls_x509_ecdsa_match = 0;
    g_net.tls_x509_ecdsa_w32 = 0;
    g_net.tls_x509_ecdsa_u1_32 = 0;
    g_net.tls_x509_ecdsa_u2_32 = 0;
    g_net.tls_x509_ecdsa_v32 = 0;

    if (!g_net.tls_x509_verify_inputs) return;
    g_net.tls_x509_ecdsa_pubkey_valid =
        p256_point_from_be32(&q,
                             g_net.tls_x509_chain_pubkey_x,
                             g_net.tls_x509_chain_pubkey_y);
    if (!p256_scalar_valid_be32(g_net.tls_x509_signature_r)) return;
    if (!p256_scalar_valid_be32(g_net.tls_x509_signature_s)) return;

    p256_scalar_from_be32(&r, g_net.tls_x509_signature_r);
    p256_scalar_from_be32(&s, g_net.tls_x509_signature_s);
    p256_scalar_from_be32(&z, g_net.tls_x509_tbs_hash);

    p256_scalar_inv(&w, &s);
    p256_scalar_mul(&u1, &z, &w);
    p256_scalar_mul(&u2, &r, &w);

    g_net.tls_x509_ecdsa_w32 = p256_fe_prefix32(&w);
    g_net.tls_x509_ecdsa_u1_32 = p256_fe_prefix32(&u1);
    g_net.tls_x509_ecdsa_u2_32 = p256_fe_prefix32(&u2);
    g_net.tls_x509_ecdsa_scalar_inputs =
        g_net.tls_x509_ecdsa_pubkey_valid ? 1 : 0;
}

int net_tls_verify_signature(void) {
    p256_point_t q;

    g_net.tls_x509_ecdsa_point_done = 0;
    g_net.tls_x509_ecdsa_match = 0;
    g_net.tls_x509_ecdsa_v32 = 0;

    if (!g_net.tls_x509_verify_inputs) return 0;
    if (!p256_point_from_be32(&q,
                              g_net.tls_x509_chain_pubkey_x,
                              g_net.tls_x509_chain_pubkey_y)) {
        g_net.tls_x509_ecdsa_pubkey_valid = 0;
        return 0;
    }
    g_net.tls_x509_ecdsa_pubkey_valid = 1;

    g_net.tls_x509_ecdsa_point_done = 1;
    g_net.tls_x509_ecdsa_match =
        net_tls_verify_ecdsa_p256(g_net.tls_x509_chain_pubkey_x,
                                  g_net.tls_x509_chain_pubkey_y,
                                  g_net.tls_x509_signature_r,
                                  g_net.tls_x509_signature_s,
                                  g_net.tls_x509_tbs_hash,
                                  &g_net.tls_x509_ecdsa_v32);
    return g_net.tls_x509_ecdsa_match;
}

int net_tls_verify_kex_signature(void) {
    uint8_t digest[32];
    uint8_t signed_data[NET_TLS_P256_SIZE * 2u + NET_TLS_KEX_PARAMS_SIZE];

    g_net.tls_kex_sig_verify_inputs = 0;
    g_net.tls_kex_sig_point_done = 0;
    g_net.tls_kex_sig_match = 0;
    g_net.tls_kex_sig_v32 = 0;

    if (!g_net.tls_server_key_exchange) return 0;
    if (!g_net.tls_kex_params_len ||
        g_net.tls_kex_params_len > NET_TLS_KEX_PARAMS_SIZE) return 0;
    if (g_net.tls_kex_sig_hash != 4 || g_net.tls_kex_sig_alg != 3) return 0;
    if (g_net.tls_x509_pubkey_alg != 2 ||
        g_net.tls_x509_pubkey_curve != 1 ||
        g_net.tls_x509_pubkey_len != 65) return 0;
    if (g_net.tls_kex_sig_r_len == 0 ||
        g_net.tls_kex_sig_r_len > NET_TLS_P256_SIZE ||
        g_net.tls_kex_sig_s_len == 0 ||
        g_net.tls_kex_sig_s_len > NET_TLS_P256_SIZE) return 0;

    memcpy(signed_data, g_net.tls_client_random, NET_TLS_P256_SIZE);
    memcpy(signed_data + NET_TLS_P256_SIZE,
           g_net.tls_server_random,
           NET_TLS_P256_SIZE);
    memcpy(signed_data + NET_TLS_P256_SIZE * 2u,
           tls_kex_params,
           g_net.tls_kex_params_len);
    sha256_hash(signed_data,
                NET_TLS_P256_SIZE * 2u + g_net.tls_kex_params_len,
                digest);
    g_net.tls_kex_params_hash32 = get32be(digest);
    g_net.tls_kex_sig_verify_inputs = 1;
    g_net.tls_kex_sig_point_done = 1;
    g_net.tls_kex_sig_match =
        net_tls_verify_ecdsa_p256(g_net.tls_x509_pubkey_x,
                                  g_net.tls_x509_pubkey_y,
                                  g_net.tls_kex_sig_r,
                                  g_net.tls_kex_sig_s,
                                  digest,
                                  &g_net.tls_kex_sig_v32);
    return g_net.tls_kex_sig_match;
}

void net_init(void) {
    memset(&g_net, 0, sizeof(g_net));
    g_net.local_ip[0] = 10;
    g_net.local_ip[1] = 0;
    g_net.local_ip[2] = 2;
    g_net.local_ip[3] = 15;
    g_net.gateway_ip[0] = 10;
    g_net.gateway_ip[1] = 0;
    g_net.gateway_ip[2] = 2;
    g_net.gateway_ip[3] = 2;
    g_net.dns_ip[0] = 10;
    g_net.dns_ip[1] = 0;
    g_net.dns_ip[2] = 2;
    g_net.dns_ip[3] = 3;
}

const net_info_t *net_info(void) {
    return &g_net;
}

static int ip_equal(const uint8_t *a, const uint8_t *b) {
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3];
}

static uint16_t ipv4_checksum(const uint8_t *data, uint16_t length) {
    uint32_t sum = 0;
    for (uint16_t i = 0; i < length; i += 2) {
        uint16_t word = (uint16_t)((uint16_t)data[i] << 8);
        if (i + 1 < length) word |= data[i + 1];
        sum += word;
        while (sum >> 16) sum = (sum & 0xFFFFu) + (sum >> 16);
    }
    return (uint16_t)(~sum);
}

static uint16_t transport_checksum(const uint8_t src_ip[4],
                                   const uint8_t dst_ip[4],
                                   uint8_t protocol,
                                   const uint8_t *segment,
                                   uint16_t segment_length) {
    uint32_t sum = 0;

    sum += (uint16_t)(((uint16_t)src_ip[0] << 8) | src_ip[1]);
    sum += (uint16_t)(((uint16_t)src_ip[2] << 8) | src_ip[3]);
    sum += (uint16_t)(((uint16_t)dst_ip[0] << 8) | dst_ip[1]);
    sum += (uint16_t)(((uint16_t)dst_ip[2] << 8) | dst_ip[3]);
    sum += protocol;
    sum += segment_length;

    for (uint16_t i = 0; i < segment_length; i += 2) {
        uint16_t word = (uint16_t)((uint16_t)segment[i] << 8);
        if (i + 1 < segment_length) word |= segment[i + 1];
        sum += word;
        while (sum >> 16) sum = (sum & 0xFFFFu) + (sum >> 16);
    }
    while (sum >> 16) sum = (sum & 0xFFFFu) + (sum >> 16);

    uint16_t result = (uint16_t)(~sum);
    return result ? result : 0xFFFFu;
}

static int net_handle_arp(const uint8_t *frame, uint16_t length) {
    if (length < sizeof(ethernet_header_t) + sizeof(arp_packet_t)) return 0;

    const ethernet_header_t *eth = (const ethernet_header_t*)frame;
    const arp_packet_t *arp = (const arp_packet_t*)(frame + sizeof(ethernet_header_t));
    if (get16be(eth->ethertype) != ETHERTYPE_ARP) return 0;
    if (get16be(arp->htype) != ARP_HTYPE_ETHERNET) return 0;
    if (get16be(arp->ptype) != ETHERTYPE_IPV4) return 0;
    if (arp->hlen != 6 || arp->plen != 4) return 0;
    if (get16be(arp->oper) != ARP_OP_REPLY) return 0;
    if (!ip_equal(arp->tpa, g_net.local_ip)) return 0;

    if (ip_equal(arp->spa, g_net.gateway_ip)) {
        memcpy(g_net.gateway_mac, arp->sha, sizeof(g_net.gateway_mac));
        g_net.gateway_mac_valid = 1;
        g_net.arp_replies++;
        return 1;
    }
    if (ip_equal(arp->spa, g_net.dns_ip)) {
        memcpy(g_net.dns_mac, arp->sha, sizeof(g_net.dns_mac));
        g_net.dns_mac_valid = 1;
        g_net.arp_replies++;
        return 1;
    }
    return 0;
}

static uint16_t dns_skip_name(const uint8_t *dns, uint16_t length, uint16_t offset) {
    while (offset < length) {
        uint8_t label = dns[offset++];
        if (label == 0) return offset;
        if ((label & 0xC0u) == 0xC0u) {
            if (offset >= length) return length;
            return (uint16_t)(offset + 1);
        }
        offset = (uint16_t)(offset + label);
    }
    return length;
}

static int net_handle_dns(const uint8_t *udp_payload, uint16_t length) {
    if (length < 12) return 0;
    g_net.last_dns_id = get16be(udp_payload + 0);
    g_net.last_dns_flags = get16be(udp_payload + 2);
    if (get16be(udp_payload + 0) != DNS_QUERY_ID) return 0;
    if ((get16be(udp_payload + 2) & 0x8000u) == 0) return 0;

    uint16_t qdcount = get16be(udp_payload + 4);
    uint16_t ancount = get16be(udp_payload + 6);
    g_net.dns_ip_count = 0;
    g_net.dns_ip_selected = 0;
    uint16_t offset = 12;
    for (uint16_t i = 0; i < qdcount; i++) {
        offset = dns_skip_name(udp_payload, length, offset);
        if (offset + 4 > length) return 0;
        offset = (uint16_t)(offset + 4);
    }

    for (uint16_t i = 0; i < ancount; i++) {
        offset = dns_skip_name(udp_payload, length, offset);
        if (offset + 10 > length) return 0;
        uint16_t type = get16be(udp_payload + offset);
        uint16_t klass = get16be(udp_payload + offset + 2);
        uint16_t rdlength = get16be(udp_payload + offset + 8);
        offset = (uint16_t)(offset + 10);
        if (offset + rdlength > length) return 0;
        if (type == 1 && klass == 1 && rdlength == 4) {
            if (g_net.dns_ip_count < 4) {
                memcpy(g_net.dns_result_ips[g_net.dns_ip_count],
                       udp_payload + offset,
                       4);
                g_net.dns_ip_count++;
            }
            if (!g_net.dns_last_ip_valid) {
                memcpy(g_net.dns_last_ip, udp_payload + offset, 4);
                g_net.dns_last_ip_valid = 1;
            }
        }
        offset = (uint16_t)(offset + rdlength);
    }
    if (g_net.dns_last_ip_valid) {
        g_net.dns_replies++;
        return 1;
    }
    return 0;
}

static void net_http_find_body(void) {
    if (g_net.http_body_offset) return;
    if (g_net.http_response_len < 4) return;

    for (uint16_t i = 0; i + 3 < g_net.http_response_len; i++) {
        if (g_net.http_response[i] == '\r' &&
            g_net.http_response[i + 1] == '\n' &&
            g_net.http_response[i + 2] == '\r' &&
            g_net.http_response[i + 3] == '\n') {
            g_net.http_body_offset = (uint16_t)(i + 4);
            return;
        }
    }
}

static void net_http_parse_status(const uint8_t *payload, uint16_t length) {
    if (!payload || length < 12 || g_net.http_valid) return;
    if (payload[0] == 'H' &&
        payload[1] == 'T' &&
        payload[2] == 'T' &&
        payload[3] == 'P' &&
        payload[8] == ' ' &&
        payload[9] >= '0' && payload[9] <= '9' &&
        payload[10] >= '0' && payload[10] <= '9' &&
        payload[11] >= '0' && payload[11] <= '9') {
        g_net.http_status = (uint16_t)((payload[9] - '0') * 100 +
                                       (payload[10] - '0') * 10 +
                                       (payload[11] - '0'));
        g_net.http_valid = 1;
    }
}

static void net_http_capture_payload(const uint8_t *payload, uint16_t length) {
    if (!payload || !length) return;
    net_http_parse_status(payload, length);
    if (g_net.http_response_len >= NET_HTTP_CAPTURE_SIZE) return;

    uint16_t room = (uint16_t)(NET_HTTP_CAPTURE_SIZE - g_net.http_response_len);
    uint16_t copy_length = length < room ? length : room;
    memcpy(g_net.http_response + g_net.http_response_len, payload, copy_length);
    g_net.http_response_len = (uint16_t)(g_net.http_response_len + copy_length);
    net_http_find_body();
}

static void net_tls_note_handshake(uint8_t type, uint32_t length, uint32_t rx) {
    g_net.tls_handshake_type = type;
    if (type == 0x0B) {
        g_net.tls_certificate = 1;
        g_net.tls_certificate_len = length;
        g_net.tls_certificate_rx = rx;
    }
    if (rx < length) {
        g_net.tls_pending_handshake_type = type;
        g_net.tls_pending_handshake_len = length;
        g_net.tls_pending_handshake_rx = rx;
    } else {
        g_net.tls_pending_handshake_type = 0;
        g_net.tls_pending_handshake_len = 0;
        g_net.tls_pending_handshake_rx = 0;
    }
}

static void net_tls_parse_kex_ecdsa_sig(const uint8_t *sig, uint16_t sig_len) {
    if (!sig || sig_len < 8 || sig[0] != 0x30) return;

    uint32_t seq_len = 0;
    uint8_t seq_header_len = 0;
    if (!net_tls_parse_der_sequence_len(sig, sig_len, &seq_len, &seq_header_len) ||
        seq_header_len + seq_len > sig_len) {
        return;
    }

    uint32_t ipos = seq_header_len;
    uint32_t r_len = 0;
    uint8_t r_header_len = 0;
    if (ipos + 2 > sig_len ||
        sig[ipos] != 0x02 ||
        !net_tls_parse_der_len_at(sig + ipos,
                                  sig_len - ipos,
                                  &r_len,
                                  &r_header_len) ||
        r_len > 255 ||
        ipos + r_header_len + r_len > sig_len) {
        return;
    }

    const uint8_t *r = sig + ipos + r_header_len;
    uint32_t r_encoded_len = r_len;
    while (r_len > 1 && *r == 0) {
        r++;
        r_len--;
    }
    g_net.tls_kex_sig_r_len = (uint8_t)r_len;
    g_net.tls_kex_sig_r32 = 0;
    if (r_len <= NET_TLS_P256_SIZE) {
        memset(g_net.tls_kex_sig_r, 0, sizeof(g_net.tls_kex_sig_r));
        memcpy(g_net.tls_kex_sig_r + NET_TLS_P256_SIZE - r_len, r, r_len);
    }
    for (uint8_t i = 0; i < 4 && i < r_len; i++) {
        g_net.tls_kex_sig_r32 = (g_net.tls_kex_sig_r32 << 8) | r[i];
    }

    ipos += r_header_len + r_encoded_len;
    uint32_t s_len = 0;
    uint8_t s_header_len = 0;
    if (ipos + 2 > sig_len ||
        sig[ipos] != 0x02 ||
        !net_tls_parse_der_len_at(sig + ipos,
                                  sig_len - ipos,
                                  &s_len,
                                  &s_header_len) ||
        s_len > 255 ||
        ipos + s_header_len + s_len > sig_len) {
        return;
    }

    const uint8_t *s = sig + ipos + s_header_len;
    while (s_len > 1 && *s == 0) {
        s++;
        s_len--;
    }
    g_net.tls_kex_sig_s_len = (uint8_t)s_len;
    g_net.tls_kex_sig_s32 = 0;
    if (s_len <= NET_TLS_P256_SIZE) {
        memset(g_net.tls_kex_sig_s, 0, sizeof(g_net.tls_kex_sig_s));
        memcpy(g_net.tls_kex_sig_s + NET_TLS_P256_SIZE - s_len, s, s_len);
    }
    for (uint8_t i = 0; i < 4 && i < s_len; i++) {
        g_net.tls_kex_sig_s32 = (g_net.tls_kex_sig_s32 << 8) | s[i];
    }
}

static void net_tls_parse_server_key_exchange(const uint8_t *body, uint32_t len) {
    g_net.tls_server_key_exchange = 1;
    g_net.tls_server_key_exchange_len = len > 0xFFFFu ? 0xFFFFu : (uint16_t)len;
    g_net.tls_kex_curve_type = 0;
    g_net.tls_kex_named_curve = 0;
    g_net.tls_kex_pubkey_len = 0;
    g_net.tls_kex_pubkey_valid = 0;
    g_net.tls_kex_pubkey_x32 = 0;
    g_net.tls_kex_pubkey_y32 = 0;
    g_net.tls_kex_sig_hash = 0;
    g_net.tls_kex_sig_alg = 0;
    g_net.tls_kex_sig_len = 0;
    g_net.tls_kex_sig_r_len = 0;
    g_net.tls_kex_sig_s_len = 0;
    g_net.tls_kex_sig_r32 = 0;
    g_net.tls_kex_sig_s32 = 0;
    g_net.tls_kex_params_len = 0;
    g_net.tls_kex_params_hash32 = 0;
    g_net.tls_kex_sig_verify_inputs = 0;
    g_net.tls_kex_sig_point_done = 0;
    g_net.tls_kex_sig_match = 0;
    g_net.tls_kex_sig_v32 = 0;
    memset(g_net.tls_kex_sig_r, 0, sizeof(g_net.tls_kex_sig_r));
    memset(g_net.tls_kex_sig_s, 0, sizeof(g_net.tls_kex_sig_s));
    memset(tls_kex_params, 0, sizeof(tls_kex_params));

    if (!body || len < 4) return;
    g_net.tls_kex_curve_type = body[0];
    g_net.tls_kex_named_curve = get16be(body + 1);
    g_net.tls_kex_pubkey_len = body[3];

    uint32_t key_pos = 4;
    uint32_t key_len = g_net.tls_kex_pubkey_len;
    if (key_pos + key_len > len) return;
    if (key_pos + key_len <= NET_TLS_KEX_PARAMS_SIZE) {
        g_net.tls_kex_params_len = (uint16_t)(key_pos + key_len);
        memcpy(tls_kex_params, body, g_net.tls_kex_params_len);
    }
    const uint8_t *key = body + key_pos;
    if (g_net.tls_kex_curve_type == 3 &&
        g_net.tls_kex_named_curve == 0x0017u &&
        key_len == 65 &&
        key[0] == 0x04) {
        p256_point_t point;
        g_net.tls_kex_pubkey_x32 = get32be(key + 1);
        g_net.tls_kex_pubkey_y32 = get32be(key + 33);
        g_net.tls_kex_pubkey_valid = p256_point_from_be32(&point,
                                                          key + 1,
                                                          key + 33);
    }

    uint32_t sig_pos = key_pos + key_len;
    if (sig_pos + 4 > len) return;
    g_net.tls_kex_sig_hash = body[sig_pos];
    g_net.tls_kex_sig_alg = body[sig_pos + 1];
    g_net.tls_kex_sig_len = get16be(body + sig_pos + 2);
    sig_pos += 4;
    if (sig_pos + g_net.tls_kex_sig_len > len) return;
    if (g_net.tls_kex_sig_alg == 3) {
        net_tls_parse_kex_ecdsa_sig(body + sig_pos, g_net.tls_kex_sig_len);
    }
}

static uint32_t get24be(const uint8_t *src) {
    return ((uint32_t)src[0] << 16) |
           ((uint32_t)src[1] << 8) |
           src[2];
}

static void net_tls_parse_certificate_header(const uint8_t *body,
                                             uint32_t length,
                                             uint32_t rx) {
    if (!body || length < 3 || rx < 3) return;

    uint32_t list_len = get24be(body);
    g_net.tls_certificate_list_len = list_len;
    if (rx >= 6 && list_len >= 3) {
        g_net.tls_first_certificate_len = get24be(body + 3);
    }
}

static int net_tls_parse_der_sequence_len(const uint8_t *der,
                                          uint32_t available,
                                          uint32_t *content_len,
                                          uint8_t *header_len) {
    if (!der || available < 2 || der[0] != 0x30) return 0;
    if ((der[1] & 0x80u) == 0) {
        *content_len = der[1];
        *header_len = 2;
        return 1;
    }

    uint8_t count = (uint8_t)(der[1] & 0x7Fu);
    if (!count || count > 3 || available < (uint32_t)(2 + count)) return 0;

    uint32_t value = 0;
    for (uint8_t i = 0; i < count; i++) {
        value = (value << 8) | der[2 + i];
    }
    *content_len = value;
    *header_len = (uint8_t)(2 + count);
    return 1;
}

static int net_tls_parse_der_len_at(const uint8_t *src,
                                    uint32_t available,
                                    uint32_t *content_len,
                                    uint8_t *header_len) {
    if (!src || available < 2) return 0;
    if ((src[1] & 0x80u) == 0) {
        *content_len = src[1];
        *header_len = 2;
        return 1;
    }

    uint8_t count = (uint8_t)(src[1] & 0x7Fu);
    if (!count || count > 3 || available < (uint32_t)(2 + count)) return 0;

    uint32_t value = 0;
    for (uint8_t i = 0; i < count; i++) {
        value = (value << 8) | src[2 + i];
    }
    *content_len = value;
    *header_len = (uint8_t)(2 + count);
    return 1;
}

static int net_tls_oid_equal(const uint8_t *oid,
                             uint8_t oid_len,
                             const uint8_t *expected,
                             uint8_t expected_len) {
    if (!oid || !expected || oid_len != expected_len) return 0;
    for (uint8_t i = 0; i < oid_len; i++) {
        if (oid[i] != expected[i]) return 0;
    }
    return 1;
}

static uint8_t net_tls_known_sig_alg(const uint8_t *oid, uint8_t oid_len) {
    static const uint8_t rsa_sha256[] =
        {0x2A,0x86,0x48,0x86,0xF7,0x0D,0x01,0x01,0x0B};
    static const uint8_t rsa_sha384[] =
        {0x2A,0x86,0x48,0x86,0xF7,0x0D,0x01,0x01,0x0C};
    static const uint8_t ecdsa_sha256[] =
        {0x2A,0x86,0x48,0xCE,0x3D,0x04,0x03,0x02};
    static const uint8_t ecdsa_sha384[] =
        {0x2A,0x86,0x48,0xCE,0x3D,0x04,0x03,0x03};

    if (net_tls_oid_equal(oid, oid_len, rsa_sha256, sizeof(rsa_sha256))) return 1;
    if (net_tls_oid_equal(oid, oid_len, rsa_sha384, sizeof(rsa_sha384))) return 2;
    if (net_tls_oid_equal(oid, oid_len, ecdsa_sha256, sizeof(ecdsa_sha256))) return 3;
    if (net_tls_oid_equal(oid, oid_len, ecdsa_sha384, sizeof(ecdsa_sha384))) return 4;
    return 0;
}

static uint8_t net_tls_known_pubkey_alg(const uint8_t *oid, uint8_t oid_len) {
    static const uint8_t rsa_encryption[] =
        {0x2A,0x86,0x48,0x86,0xF7,0x0D,0x01,0x01,0x01};
    static const uint8_t ec_public_key[] =
        {0x2A,0x86,0x48,0xCE,0x3D,0x02,0x01};

    if (net_tls_oid_equal(oid, oid_len, rsa_encryption, sizeof(rsa_encryption))) return 1;
    if (net_tls_oid_equal(oid, oid_len, ec_public_key, sizeof(ec_public_key))) return 2;
    return 0;
}

static uint8_t net_tls_known_curve(const uint8_t *oid, uint8_t oid_len) {
    static const uint8_t prime256v1[] =
        {0x2A,0x86,0x48,0xCE,0x3D,0x03,0x01,0x07};
    static const uint8_t secp384r1[] =
        {0x2B,0x81,0x04,0x00,0x22};
    static const uint8_t secp521r1[] =
        {0x2B,0x81,0x04,0x00,0x23};

    if (net_tls_oid_equal(oid, oid_len, prime256v1, sizeof(prime256v1))) return 1;
    if (net_tls_oid_equal(oid, oid_len, secp384r1, sizeof(secp384r1))) return 2;
    if (net_tls_oid_equal(oid, oid_len, secp521r1, sizeof(secp521r1))) return 3;
    return 0;
}

static int net_tls_digit(uint8_t value) {
    return value >= '0' && value <= '9';
}

static uint8_t net_tls_two_digits(const uint8_t *src) {
    return (uint8_t)((src[0] - '0') * 10 + (src[1] - '0'));
}

static uint32_t net_tls_parse_asn1_date(uint8_t tag,
                                        const uint8_t *src,
                                        uint32_t len) {
    uint16_t year;
    uint8_t month;
    uint8_t day;

    if (tag == 0x17) {
        if (!src || len < 6) return 0;
        for (uint8_t i = 0; i < 6; i++) {
            if (!net_tls_digit(src[i])) return 0;
        }
        uint8_t yy = net_tls_two_digits(src);
        year = (uint16_t)(yy >= 50 ? 1900 + yy : 2000 + yy);
        month = net_tls_two_digits(src + 2);
        day = net_tls_two_digits(src + 4);
    } else if (tag == 0x18) {
        if (!src || len < 8) return 0;
        for (uint8_t i = 0; i < 8; i++) {
            if (!net_tls_digit(src[i])) return 0;
        }
        year = (uint16_t)((src[0] - '0') * 1000 +
                          (src[1] - '0') * 100 +
                          (src[2] - '0') * 10 +
                          (src[3] - '0'));
        month = net_tls_two_digits(src + 4);
        day = net_tls_two_digits(src + 6);
    } else {
        return 0;
    }

    if (month < 1 || month > 12 || day < 1 || day > 31) return 0;
    return (uint32_t)year * 10000u + (uint32_t)month * 100u + day;
}

static int net_tls_is_printable_byte(uint8_t value) {
    return value >= 32 && value <= 126;
}

static void net_tls_copy_name_string(const uint8_t *src,
                                     uint32_t len,
                                     char *dst,
                                     uint8_t *dst_len) {
    uint8_t out = 0;
    if (!src || !dst || !dst_len) return;

    uint32_t limit = len;
    if (limit >= NET_X509_NAME_SIZE) limit = NET_X509_NAME_SIZE - 1;
    for (uint32_t i = 0; i < limit; i++) {
        dst[out++] = net_tls_is_printable_byte(src[i]) ? (char)src[i] : '?';
    }
    dst[out] = 0;
    *dst_len = out;
}

static uint16_t net_tls_strlen(const char *s) {
    uint16_t len = 0;
    while (s && s[len]) len++;
    return len;
}

static int net_tls_str_equal(const char *a, const char *b) {
    uint16_t i = 0;
    if (!a || !b) return 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return 0;
        i++;
    }
    return a[i] == 0 && b[i] == 0;
}

static int net_tls_known_issuer_cn(const char *cn) {
    return net_tls_str_equal(cn, "Cloudflare TLS Issuing ECC CA 1") ||
           net_tls_str_equal(cn, "E7") ||
           net_tls_str_equal(cn, "E8") ||
           net_tls_str_equal(cn, "R12") ||
           net_tls_str_equal(cn, "R13") ||
           net_tls_str_equal(cn, "DigiCert Global G2 TLS RSA SHA256 2020 CA1");
}

static int net_tls_dns_matches(const char *host, const char *dns) {
    if (net_tls_str_equal(host, dns)) return 1;
    if (!host || !dns || dns[0] != '*' || dns[1] != '.') return 0;

    const char *suffix = dns + 1;
    uint16_t host_len = net_tls_strlen(host);
    uint16_t suffix_len = net_tls_strlen(suffix);
    if (host_len <= suffix_len) return 0;

    uint16_t offset = (uint16_t)(host_len - suffix_len);
    if (host[offset - 1] == '.') return 0;
    for (uint16_t i = 0; i < offset; i++) {
        if (host[i] == '.') return 0;
    }
    return net_tls_str_equal(host + offset, suffix);
}

static void net_tls_parse_name_cn(const uint8_t *name,
                                  uint32_t name_len,
                                  char *dst,
                                  uint8_t *dst_len) {
    static const uint8_t common_name_oid[] = {0x55, 0x04, 0x03};

    if (!name || !dst || !dst_len) return;
    dst[0] = 0;
    *dst_len = 0;

    for (uint32_t pos = 0; pos + 8 < name_len; pos++) {
        if (name[pos] != 0x06) continue;
        uint32_t oid_len = 0;
        uint8_t oid_header_len = 0;
        if (!net_tls_parse_der_len_at(name + pos,
                                      name_len - pos,
                                      &oid_len,
                                      &oid_header_len) ||
            oid_len != sizeof(common_name_oid) ||
            pos + oid_header_len + oid_len >= name_len ||
            !net_tls_oid_equal(name + pos + oid_header_len,
                               (uint8_t)oid_len,
                               common_name_oid,
                               sizeof(common_name_oid))) {
            continue;
        }

        uint32_t value_pos = pos + oid_header_len + oid_len;
        while (value_pos < name_len &&
               name[value_pos] != 0x0C &&
               name[value_pos] != 0x13 &&
               name[value_pos] != 0x16) {
            value_pos++;
        }
        if (value_pos + 2 >= name_len) return;

        uint32_t value_len = 0;
        uint8_t value_header_len = 0;
        if (!net_tls_parse_der_len_at(name + value_pos,
                                      name_len - value_pos,
                                      &value_len,
                                      &value_header_len) ||
            value_pos + value_header_len + value_len > name_len) {
            return;
        }
        net_tls_copy_name_string(name + value_pos + value_header_len,
                                 value_len,
                                 dst,
                                 dst_len);
        return;
    }
}

static void net_tls_parse_san_dns(const uint8_t *src, uint32_t len) {
    if (!src || !len) return;

    for (uint32_t pos = 0; pos + 6 < len; pos++) {
        if (src[pos] != 0x06 ||
            pos + 5 > len ||
            src[pos + 1] != 3 ||
            src[pos + 2] != 0x55 ||
            src[pos + 3] != 0x1D ||
            src[pos + 4] != 0x11) {
            continue;
        }

        uint32_t cursor = pos + 5;
        while (cursor + 2 < len && src[cursor] != 0x30) cursor++;
        if (cursor + 2 >= len) return;

        uint32_t seq_len = 0;
        uint8_t seq_header_len = 0;
        if (!net_tls_parse_der_sequence_len(src + cursor,
                                            len - cursor,
                                            &seq_len,
                                            &seq_header_len) ||
            cursor + seq_header_len + seq_len > len) {
            return;
        }

        uint32_t end = cursor + seq_header_len + seq_len;
        cursor += seq_header_len;
        while (cursor + 2 <= end) {
            uint8_t tag = src[cursor++];
            uint32_t name_len = 0;
            if ((src[cursor] & 0x80u) == 0) {
                name_len = src[cursor++];
            } else {
                return;
            }
            if (cursor + name_len > end) return;
            if (tag == 0x82) {
                net_tls_copy_name_string(src + cursor,
                                         name_len,
                                         g_net.tls_x509_san_dns,
                                         &g_net.tls_x509_san_dns_len);
                g_net.tls_x509_host_match =
                    net_tls_dns_matches(g_net.http_host, g_net.tls_x509_san_dns);
                return;
            }
            cursor += name_len;
        }
        return;
    }
}

static void net_tls_parse_basic_constraints(const uint8_t *src, uint32_t len) {
    if (!src || !len) return;

    for (uint32_t pos = 0; pos + 6 < len; pos++) {
        if (src[pos] != 0x06 ||
            pos + 5 > len ||
            src[pos + 1] != 3 ||
            src[pos + 2] != 0x55 ||
            src[pos + 3] != 0x1D ||
            src[pos + 4] != 0x13) {
            continue;
        }

        uint32_t cursor = pos + 5;
        while (cursor + 2 < len && src[cursor] != 0x04) cursor++;
        if (cursor + 2 >= len) return;

        uint32_t octet_len = 0;
        uint8_t octet_header_len = 0;
        if (!net_tls_parse_der_len_at(src + cursor,
                                      len - cursor,
                                      &octet_len,
                                      &octet_header_len) ||
            cursor + octet_header_len + octet_len > len) {
            return;
        }

        uint32_t inner = cursor + octet_header_len;
        uint32_t inner_len = octet_len;
        uint32_t seq_len = 0;
        uint8_t seq_header_len = 0;
        if (!net_tls_parse_der_sequence_len(src + inner,
                                            inner_len,
                                            &seq_len,
                                            &seq_header_len) ||
            seq_header_len + seq_len > inner_len) {
            return;
        }

        g_net.tls_x509_basic_constraints = 1;
        uint32_t bpos = inner + seq_header_len;
        uint32_t bend = inner + seq_header_len + seq_len;
        if (bpos + 3 <= bend && src[bpos] == 0x01 && src[bpos + 1] == 0x01) {
            g_net.tls_x509_is_ca = src[bpos + 2] ? 1 : 0;
        } else {
            g_net.tls_x509_is_ca = 0;
        }
        return;
    }
}

static void net_tls_parse_key_usage(const uint8_t *src, uint32_t len) {
    if (!src || !len) return;

    for (uint32_t pos = 0; pos + 6 < len; pos++) {
        if (src[pos] != 0x06 ||
            pos + 5 > len ||
            src[pos + 1] != 3 ||
            src[pos + 2] != 0x55 ||
            src[pos + 3] != 0x1D ||
            src[pos + 4] != 0x0F) {
            continue;
        }

        uint32_t cursor = pos + 5;
        while (cursor + 2 < len && src[cursor] != 0x04) cursor++;
        if (cursor + 2 >= len) return;

        uint32_t octet_len = 0;
        uint8_t octet_header_len = 0;
        if (!net_tls_parse_der_len_at(src + cursor,
                                      len - cursor,
                                      &octet_len,
                                      &octet_header_len) ||
            cursor + octet_header_len + octet_len > len) {
            return;
        }

        uint32_t inner = cursor + octet_header_len;
        if (octet_len < 4 || src[inner] != 0x03) return;

        uint32_t bit_len = 0;
        uint8_t bit_header_len = 0;
        if (!net_tls_parse_der_len_at(src + inner,
                                      octet_len,
                                      &bit_len,
                                      &bit_header_len) ||
            bit_len < 2 ||
            bit_header_len + bit_len > octet_len) {
            return;
        }

        uint8_t usage = src[inner + bit_header_len + 1];
        g_net.tls_x509_key_usage = 1;
        g_net.tls_x509_key_usage_bits = 0;
        if (usage & 0x80u) g_net.tls_x509_key_usage_bits |= 1; /* digitalSignature */
        if (usage & 0x20u) g_net.tls_x509_key_usage_bits |= 2; /* keyEncipherment */
        if (usage & 0x04u) g_net.tls_x509_key_usage_bits |= 4; /* keyCertSign */
        return;
    }
}

static void net_tls_parse_extended_key_usage(const uint8_t *src, uint32_t len) {
    static const uint8_t server_auth[] = {0x2B,0x06,0x01,0x05,0x05,0x07,0x03,0x01};
    static const uint8_t client_auth[] = {0x2B,0x06,0x01,0x05,0x05,0x07,0x03,0x02};

    if (!src || !len) return;

    for (uint32_t pos = 0; pos + 6 < len; pos++) {
        if (src[pos] != 0x06 ||
            pos + 5 > len ||
            src[pos + 1] != 3 ||
            src[pos + 2] != 0x55 ||
            src[pos + 3] != 0x1D ||
            src[pos + 4] != 0x25) {
            continue;
        }

        uint32_t cursor = pos + 5;
        while (cursor + 2 < len && src[cursor] != 0x04) cursor++;
        if (cursor + 2 >= len) return;

        uint32_t octet_len = 0;
        uint8_t octet_header_len = 0;
        if (!net_tls_parse_der_len_at(src + cursor,
                                      len - cursor,
                                      &octet_len,
                                      &octet_header_len) ||
            cursor + octet_header_len + octet_len > len) {
            return;
        }

        uint32_t inner = cursor + octet_header_len;
        uint32_t seq_len = 0;
        uint8_t seq_header_len = 0;
        if (!net_tls_parse_der_sequence_len(src + inner,
                                            octet_len,
                                            &seq_len,
                                            &seq_header_len) ||
            seq_header_len + seq_len > octet_len) {
            return;
        }

        uint32_t oid_pos = inner + seq_header_len;
        uint32_t end = oid_pos + seq_len;
        g_net.tls_x509_eku = 1;
        g_net.tls_x509_eku_bits = 0;
        while (oid_pos + 2 <= end) {
            if (src[oid_pos] != 0x06) return;
            uint32_t oid_len = 0;
            uint8_t oid_header_len = 0;
            if (!net_tls_parse_der_len_at(src + oid_pos,
                                          end - oid_pos,
                                          &oid_len,
                                          &oid_header_len) ||
                oid_len > 32 ||
                oid_pos + oid_header_len + oid_len > end) {
                return;
            }
            const uint8_t *oid = src + oid_pos + oid_header_len;
            if (net_tls_oid_equal(oid, (uint8_t)oid_len, server_auth, sizeof(server_auth))) {
                g_net.tls_x509_eku_bits |= 1;
            }
            if (net_tls_oid_equal(oid, (uint8_t)oid_len, client_auth, sizeof(client_auth))) {
                g_net.tls_x509_eku_bits |= 2;
            }
            oid_pos += oid_header_len + oid_len;
        }
        return;
    }
}

static void net_tls_parse_certificate_signature(const uint8_t *der,
                                                uint32_t available,
                                                uint32_t pos) {
    if (!der || available <= pos + 4 || der[pos] != 0x30) return;

    uint32_t alg_len = 0;
    uint8_t alg_header_len = 0;
    if (!net_tls_parse_der_sequence_len(der + pos,
                                        available - pos,
                                        &alg_len,
                                        &alg_header_len)) {
        return;
    }

    uint32_t oid_pos = pos + alg_header_len;
    if (available > oid_pos + 2 && der[oid_pos] == 0x06) {
        uint32_t oid_len = 0;
        uint8_t oid_header_len = 0;
        if (net_tls_parse_der_len_at(der + oid_pos,
                                     available - oid_pos,
                                     &oid_len,
                                     &oid_header_len) &&
            oid_len <= 32 &&
            available >= oid_pos + oid_header_len + oid_len) {
            g_net.tls_x509_outer_sig_oid_len = (uint8_t)oid_len;
            g_net.tls_x509_outer_sig_alg =
                net_tls_known_sig_alg(der + oid_pos + oid_header_len,
                                      (uint8_t)oid_len);
        }
    }

    pos += alg_header_len + alg_len;
    if (available <= pos + 2 || der[pos] != 0x03) return;

    uint32_t bit_len = 0;
    uint8_t bit_header_len = 0;
    if (!net_tls_parse_der_len_at(der + pos,
                                  available - pos,
                                  &bit_len,
                                  &bit_header_len) ||
        bit_len < 1 ||
        bit_len > 0xFFFFu ||
        available < pos + bit_header_len + bit_len) {
        return;
    }

    g_net.tls_x509_signature_unused_bits = der[pos + bit_header_len];
    g_net.tls_x509_signature_len = (uint16_t)(bit_len - 1u);
    if (g_net.tls_x509_outer_sig_alg == 3 &&
        g_net.tls_x509_signature_unused_bits == 0) {
        const uint8_t *sig = der + pos + bit_header_len + 1u;
        uint32_t sig_len = bit_len - 1u;
        uint32_t seq_len = 0;
        uint8_t seq_header_len = 0;
        if (!net_tls_parse_der_sequence_len(sig,
                                            sig_len,
                                            &seq_len,
                                            &seq_header_len) ||
            seq_header_len + seq_len > sig_len) {
            return;
        }

        uint32_t ipos = seq_header_len;
        uint32_t r_len = 0;
        uint8_t r_header_len = 0;
        if (ipos + 2 > sig_len ||
            sig[ipos] != 0x02 ||
            !net_tls_parse_der_len_at(sig + ipos,
                                      sig_len - ipos,
                                      &r_len,
                                      &r_header_len) ||
            r_len > 255 ||
            ipos + r_header_len + r_len > sig_len) {
            return;
        }

        const uint8_t *r = sig + ipos + r_header_len;
        uint32_t r_encoded_len = r_len;
        while (r_len > 1 && *r == 0) {
            r++;
            r_len--;
        }
        g_net.tls_x509_signature_r_len = (uint8_t)r_len;
        g_net.tls_x509_signature_r32 = 0;
        if (r_len <= NET_TLS_P256_SIZE) {
            memset(g_net.tls_x509_signature_r, 0, sizeof(g_net.tls_x509_signature_r));
            memcpy(g_net.tls_x509_signature_r + NET_TLS_P256_SIZE - r_len, r, r_len);
        }
        for (uint8_t i = 0; i < 4 && i < r_len; i++) {
            g_net.tls_x509_signature_r32 =
                (g_net.tls_x509_signature_r32 << 8) | r[i];
        }

        ipos += r_header_len + r_encoded_len;
        uint32_t s_len = 0;
        uint8_t s_header_len = 0;
        if (ipos + 2 > sig_len ||
            sig[ipos] != 0x02 ||
            !net_tls_parse_der_len_at(sig + ipos,
                                      sig_len - ipos,
                                      &s_len,
                                      &s_header_len) ||
            s_len > 255 ||
            ipos + s_header_len + s_len > sig_len) {
            return;
        }

        const uint8_t *s = sig + ipos + s_header_len;
        while (s_len > 1 && *s == 0) {
            s++;
            s_len--;
        }
        g_net.tls_x509_signature_s_len = (uint8_t)s_len;
        g_net.tls_x509_signature_s32 = 0;
        if (s_len <= NET_TLS_P256_SIZE) {
            memset(g_net.tls_x509_signature_s, 0, sizeof(g_net.tls_x509_signature_s));
            memcpy(g_net.tls_x509_signature_s + NET_TLS_P256_SIZE - s_len, s, s_len);
        }
        for (uint8_t i = 0; i < 4 && i < s_len; i++) {
            g_net.tls_x509_signature_s32 =
                (g_net.tls_x509_signature_s32 << 8) | s[i];
        }
    }
}

static void net_tls_parse_first_certificate_der(const uint8_t *body,
                                                uint32_t rx) {
    if (!body || rx < 8 || !g_net.tls_first_certificate_len) return;

    const uint8_t *der = body + 6;
    uint32_t available = rx - 6;
    uint32_t content_len = 0;
    uint8_t header_len = 0;
    if (!net_tls_parse_der_sequence_len(der, available, &content_len, &header_len)) {
        return;
    }

    g_net.tls_first_certificate_der = 1;
    g_net.tls_first_certificate_der_header_len = header_len;
    g_net.tls_first_certificate_der_len = content_len + header_len;

    uint32_t pos = header_len;
    uint32_t tbs_content_len = 0;
    uint8_t tbs_header_len = 0;
    if (available < pos + 2 || der[pos] != 0x30) return;
    if (!net_tls_parse_der_sequence_len(der + pos,
                                        available - pos,
                                        &tbs_content_len,
                                        &tbs_header_len)) {
        return;
    }
    g_net.tls_x509_tbs = 1;
    g_net.tls_x509_tbs_len = tbs_content_len + tbs_header_len;
    net_tls_parse_certificate_signature(der,
                                        available,
                                        pos + tbs_header_len + tbs_content_len);
    if (g_net.tls_x509_outer_sig_alg == 1 ||
        g_net.tls_x509_outer_sig_alg == 3) {
        uint8_t digest[32];
        sha256_hash(der + pos, g_net.tls_x509_tbs_len, digest);
        g_net.tls_x509_tbs_hash_alg = 1;
        g_net.tls_x509_tbs_hash32 = get32be(digest);
        memcpy(g_net.tls_x509_tbs_hash, digest, sizeof(g_net.tls_x509_tbs_hash));
    }

    pos += tbs_header_len;
    if (available > pos + 2 && der[pos] == 0xA0) {
        uint32_t version_len = 0;
        uint8_t version_header_len = 0;
        if (!net_tls_parse_der_len_at(der + pos,
                                      available - pos,
                                      &version_len,
                                      &version_header_len)) {
            return;
        }
        pos += version_header_len + version_len;
    }

    if (available > pos + 2 && der[pos] == 0x02) {
        uint32_t serial_len = 0;
        uint8_t serial_header_len = 0;
        if (net_tls_parse_der_len_at(der + pos,
                                     available - pos,
                                     &serial_len,
                                     &serial_header_len) &&
            serial_len <= 255) {
            g_net.tls_x509_serial_len = (uint8_t)serial_len;
            pos += serial_header_len + serial_len;
        }
    }

    if (available > pos + 4 && der[pos] == 0x30) {
        uint32_t alg_len = 0;
        uint8_t alg_header_len = 0;
        if (!net_tls_parse_der_sequence_len(der + pos,
                                            available - pos,
                                            &alg_len,
                                            &alg_header_len)) {
            return;
        }
        uint32_t oid_pos = pos + alg_header_len;
        if (available > oid_pos + 2 && der[oid_pos] == 0x06) {
            uint32_t oid_len = 0;
            uint8_t oid_header_len = 0;
            if (net_tls_parse_der_len_at(der + oid_pos,
                                         available - oid_pos,
                                         &oid_len,
                                         &oid_header_len) &&
                oid_len <= 32 &&
                available >= oid_pos + oid_header_len + oid_len) {
                g_net.tls_x509_sig_oid_len = (uint8_t)oid_len;
                g_net.tls_x509_sig_alg =
                    net_tls_known_sig_alg(der + oid_pos + oid_header_len,
                                          (uint8_t)oid_len);
            }
        }
        if (available < pos + alg_header_len + alg_len) return;
        pos += alg_header_len + alg_len;
    }

    if (available > pos + 2 && der[pos] == 0x30) {
        uint32_t issuer_len = 0;
        uint8_t issuer_header_len = 0;
        if (!net_tls_parse_der_sequence_len(der + pos,
                                            available - pos,
                                            &issuer_len,
                                            &issuer_header_len)) {
            return;
        }
        if (available < pos + issuer_header_len + issuer_len) return;
        net_tls_parse_name_cn(der + pos + issuer_header_len,
                              issuer_len,
                              g_net.tls_x509_issuer_cn,
                              &g_net.tls_x509_issuer_cn_len);
        g_net.tls_x509_known_issuer =
            net_tls_known_issuer_cn(g_net.tls_x509_issuer_cn) ? 1 : 0;
        pos += issuer_header_len + issuer_len;
    }

    if (available > pos + 4 && der[pos] == 0x30) {
        uint32_t validity_len = 0;
        uint8_t validity_header_len = 0;
        if (!net_tls_parse_der_sequence_len(der + pos,
                                            available - pos,
                                            &validity_len,
                                            &validity_header_len)) {
            return;
        }
        uint32_t vpos = pos + validity_header_len;
        uint32_t vend = vpos + validity_len;
        if (vend > available) return;

        uint8_t tags[2] = {0, 0};
        uint8_t lens[2] = {0, 0};
        for (uint8_t i = 0; i < 2; i++) {
            if (vpos + 2 > vend) return;
            if (der[vpos] != 0x17 && der[vpos] != 0x18) return;
            uint32_t time_len = 0;
            uint8_t time_header_len = 0;
            if (!net_tls_parse_der_len_at(der + vpos,
                                          vend - vpos,
                                          &time_len,
                                          &time_header_len) ||
                time_len > 255 ||
                vpos + time_header_len + time_len > vend) {
                return;
            }
            tags[i] = der[vpos];
            lens[i] = (uint8_t)time_len;
            uint32_t date = net_tls_parse_asn1_date(der[vpos],
                                                    der + vpos + time_header_len,
                                                    time_len);
            if (i == 0) {
                g_net.tls_x509_not_before_date = date;
            } else {
                g_net.tls_x509_not_after_date = date;
            }
            vpos += time_header_len + time_len;
        }

        g_net.tls_x509_validity = 1;
        g_net.tls_x509_not_before_tag = tags[0];
        g_net.tls_x509_not_before_len = lens[0];
        g_net.tls_x509_not_after_tag = tags[1];
        g_net.tls_x509_not_after_len = lens[1];
        pos = vend;
    }

    if (available > pos + 2 && der[pos] == 0x30) {
        uint32_t subject_len = 0;
        uint8_t subject_header_len = 0;
        if (!net_tls_parse_der_sequence_len(der + pos,
                                            available - pos,
                                            &subject_len,
                                            &subject_header_len)) {
            return;
        }
        if (available < pos + subject_header_len + subject_len) return;
        net_tls_parse_name_cn(der + pos + subject_header_len,
                              subject_len,
                              g_net.tls_x509_subject_cn,
                              &g_net.tls_x509_subject_cn_len);
        pos += subject_header_len + subject_len;
    }

    if (available > pos + 4 && der[pos] == 0x30) {
        uint32_t spki_len = 0;
        uint8_t spki_header_len = 0;
        if (!net_tls_parse_der_sequence_len(der + pos,
                                            available - pos,
                                            &spki_len,
                                            &spki_header_len)) {
            return;
        }
        uint32_t spki_end = pos + spki_header_len + spki_len;
        uint32_t alg_pos = pos + spki_header_len;
        if (spki_end > available || spki_end <= alg_pos + 4) return;
        if (der[alg_pos] == 0x30) {
            uint32_t alg_len = 0;
            uint8_t alg_header_len = 0;
            if (!net_tls_parse_der_sequence_len(der + alg_pos,
                                                spki_end - alg_pos,
                                                &alg_len,
                                                &alg_header_len)) {
                return;
            }
            uint32_t alg_end = alg_pos + alg_header_len + alg_len;
            uint32_t oid_pos = alg_pos + alg_header_len;
            if (alg_end <= spki_end && alg_end > oid_pos + 2 && der[oid_pos] == 0x06) {
                uint32_t oid_len = 0;
                uint8_t oid_header_len = 0;
                if (net_tls_parse_der_len_at(der + oid_pos,
                                             alg_end - oid_pos,
                                             &oid_len,
                                             &oid_header_len) &&
                    oid_len <= 32 &&
                    alg_end >= oid_pos + oid_header_len + oid_len) {
                    g_net.tls_x509_pubkey_oid_len = (uint8_t)oid_len;
                    g_net.tls_x509_pubkey_alg =
                        net_tls_known_pubkey_alg(der + oid_pos + oid_header_len,
                                                 (uint8_t)oid_len);
                }

                uint32_t curve_pos = oid_pos + oid_header_len + oid_len;
                if (alg_end > curve_pos + 2 && der[curve_pos] == 0x06) {
                    uint32_t curve_len = 0;
                    uint8_t curve_header_len = 0;
                    if (net_tls_parse_der_len_at(der + curve_pos,
                                                 alg_end - curve_pos,
                                                 &curve_len,
                                                 &curve_header_len) &&
                        curve_len <= 32 &&
                        alg_end >= curve_pos + curve_header_len + curve_len) {
                        g_net.tls_x509_pubkey_curve =
                            net_tls_known_curve(der + curve_pos + curve_header_len,
                                                (uint8_t)curve_len);
                    }
                }
                uint32_t bit_pos = alg_end;
                if (spki_end > bit_pos + 2 && der[bit_pos] == 0x03) {
                    uint32_t bit_len = 0;
                    uint8_t bit_header_len = 0;
                    if (net_tls_parse_der_len_at(der + bit_pos,
                                                 spki_end - bit_pos,
                                                 &bit_len,
                                                 &bit_header_len) &&
                        bit_len >= 2 &&
                        spki_end >= bit_pos + bit_header_len + bit_len) {
                        const uint8_t *point = der + bit_pos + bit_header_len + 1u;
                        uint32_t point_len = bit_len - 1u;
                        if (point_len <= 0xFFFFu) {
                            g_net.tls_x509_pubkey_len = (uint16_t)point_len;
                        }
                        if (g_net.tls_x509_pubkey_alg == 2 &&
                            g_net.tls_x509_pubkey_curve == 1 &&
                            point_len == 65 &&
                            point[0] == 0x04) {
                            g_net.tls_x509_pubkey_x32 = get32be(point + 1u);
                            g_net.tls_x509_pubkey_y32 = get32be(point + 33u);
                            memcpy(g_net.tls_x509_pubkey_x,
                                   point + 1u,
                                   NET_TLS_P256_SIZE);
                            memcpy(g_net.tls_x509_pubkey_y,
                                   point + 33u,
                                   NET_TLS_P256_SIZE);
                        }
                    }
                }
            }
        }
        pos = spki_end;
    }

    net_tls_parse_san_dns(der + pos, available - pos);
    net_tls_parse_basic_constraints(der + pos, available - pos);
    net_tls_parse_key_usage(der + pos, available - pos);
    net_tls_parse_extended_key_usage(der + pos, available - pos);
}

static void net_tls_parse_chain_certificate_der(const uint8_t *body,
                                                uint32_t rx) {
    if (!body || rx < 9 || !g_net.tls_first_certificate_len) return;

    uint32_t second_len_pos = 6u + g_net.tls_first_certificate_len;
    if (rx < second_len_pos + 3u) return;

    uint32_t second_len = get24be(body + second_len_pos);
    if (!second_len || rx < second_len_pos + 3u + second_len) return;

    const uint8_t *der = body + second_len_pos + 3u;
    uint32_t available = rx - second_len_pos - 3u;
    uint32_t content_len = 0;
    uint8_t header_len = 0;
    if (!net_tls_parse_der_sequence_len(der, available, &content_len, &header_len)) {
        return;
    }

    g_net.tls_second_certificate_len = second_len;
    g_net.tls_second_certificate_der = 1;

    uint32_t pos = header_len;
    uint32_t tbs_content_len = 0;
    uint8_t tbs_header_len = 0;
    if (available < pos + 2 || der[pos] != 0x30) return;
    if (!net_tls_parse_der_sequence_len(der + pos,
                                        available - pos,
                                        &tbs_content_len,
                                        &tbs_header_len)) {
        return;
    }

    pos += tbs_header_len;
    if (available > pos + 2 && der[pos] == 0xA0) {
        uint32_t version_len = 0;
        uint8_t version_header_len = 0;
        if (!net_tls_parse_der_len_at(der + pos,
                                      available - pos,
                                      &version_len,
                                      &version_header_len)) {
            return;
        }
        pos += version_header_len + version_len;
    }

    if (available > pos + 2 && der[pos] == 0x02) {
        uint32_t serial_len = 0;
        uint8_t serial_header_len = 0;
        if (!net_tls_parse_der_len_at(der + pos,
                                      available - pos,
                                      &serial_len,
                                      &serial_header_len)) {
            return;
        }
        pos += serial_header_len + serial_len;
    }

    if (available > pos + 4 && der[pos] == 0x30) {
        uint32_t alg_len = 0;
        uint8_t alg_header_len = 0;
        if (!net_tls_parse_der_sequence_len(der + pos,
                                            available - pos,
                                            &alg_len,
                                            &alg_header_len)) {
            return;
        }
        pos += alg_header_len + alg_len;
    }

    if (available > pos + 2 && der[pos] == 0x30) {
        uint32_t issuer_len = 0;
        uint8_t issuer_header_len = 0;
        if (!net_tls_parse_der_sequence_len(der + pos,
                                            available - pos,
                                            &issuer_len,
                                            &issuer_header_len)) {
            return;
        }
        pos += issuer_header_len + issuer_len;
    }

    if (available > pos + 4 && der[pos] == 0x30) {
        uint32_t validity_len = 0;
        uint8_t validity_header_len = 0;
        if (!net_tls_parse_der_sequence_len(der + pos,
                                            available - pos,
                                            &validity_len,
                                            &validity_header_len)) {
            return;
        }
        pos += validity_header_len + validity_len;
    }

    if (available > pos + 2 && der[pos] == 0x30) {
        uint32_t subject_len = 0;
        uint8_t subject_header_len = 0;
        if (!net_tls_parse_der_sequence_len(der + pos,
                                            available - pos,
                                            &subject_len,
                                            &subject_header_len)) {
            return;
        }
        if (available < pos + subject_header_len + subject_len) return;
        net_tls_parse_name_cn(der + pos + subject_header_len,
                              subject_len,
                              g_net.tls_x509_chain_subject_cn,
                              &g_net.tls_x509_chain_subject_cn_len);
        g_net.tls_chain_link =
            net_tls_str_equal(g_net.tls_x509_issuer_cn,
                              g_net.tls_x509_chain_subject_cn) ? 1 : 0;
        pos += subject_header_len + subject_len;
    }

    if (available > pos + 4 && der[pos] == 0x30) {
        uint32_t spki_len = 0;
        uint8_t spki_header_len = 0;
        if (!net_tls_parse_der_sequence_len(der + pos,
                                            available - pos,
                                            &spki_len,
                                            &spki_header_len)) {
            return;
        }
        uint32_t spki_end = pos + spki_header_len + spki_len;
        uint32_t alg_pos = pos + spki_header_len;
        if (spki_end > available || spki_end <= alg_pos + 4 || der[alg_pos] != 0x30) {
            return;
        }

        uint32_t alg_len = 0;
        uint8_t alg_header_len = 0;
        if (!net_tls_parse_der_sequence_len(der + alg_pos,
                                            spki_end - alg_pos,
                                            &alg_len,
                                            &alg_header_len)) {
            return;
        }
        uint32_t alg_end = alg_pos + alg_header_len + alg_len;
        uint32_t oid_pos = alg_pos + alg_header_len;
        if (alg_end > spki_end || alg_end <= oid_pos + 2 || der[oid_pos] != 0x06) {
            return;
        }

        uint32_t oid_len = 0;
        uint8_t oid_header_len = 0;
        if (!net_tls_parse_der_len_at(der + oid_pos,
                                      alg_end - oid_pos,
                                      &oid_len,
                                      &oid_header_len) ||
            oid_len > 32 ||
            alg_end < oid_pos + oid_header_len + oid_len) {
            return;
        }
        g_net.tls_x509_chain_pubkey_alg =
            net_tls_known_pubkey_alg(der + oid_pos + oid_header_len,
                                     (uint8_t)oid_len);

        uint32_t curve_pos = oid_pos + oid_header_len + oid_len;
        if (alg_end > curve_pos + 2 && der[curve_pos] == 0x06) {
            uint32_t curve_len = 0;
            uint8_t curve_header_len = 0;
            if (net_tls_parse_der_len_at(der + curve_pos,
                                         alg_end - curve_pos,
                                         &curve_len,
                                         &curve_header_len) &&
                curve_len <= 32 &&
                alg_end >= curve_pos + curve_header_len + curve_len) {
                g_net.tls_x509_chain_curve =
                    net_tls_known_curve(der + curve_pos + curve_header_len,
                                        (uint8_t)curve_len);
            }
        }

        uint32_t bit_pos = alg_end;
        if (spki_end <= bit_pos + 2 || der[bit_pos] != 0x03) return;

        uint32_t bit_len = 0;
        uint8_t bit_header_len = 0;
        if (!net_tls_parse_der_len_at(der + bit_pos,
                                      spki_end - bit_pos,
                                      &bit_len,
                                      &bit_header_len) ||
            bit_len < 2 ||
            spki_end < bit_pos + bit_header_len + bit_len) {
            return;
        }

        const uint8_t *point = der + bit_pos + bit_header_len + 1u;
        uint32_t point_len = bit_len - 1u;
        g_net.tls_x509_chain_pubkey_len =
            point_len > 0xFFFFu ? 0xFFFFu : (uint16_t)point_len;
        if (point_len >= 65 && point[0] == 0x04) {
            uint32_t coord_len = (point_len - 1u) / 2u;
            g_net.tls_x509_chain_pubkey_x32 = get32be(point + 1u);
            g_net.tls_x509_chain_pubkey_y32 = get32be(point + 1u + coord_len);
            if (coord_len == NET_TLS_P256_SIZE) {
                memcpy(g_net.tls_x509_chain_pubkey_x, point + 1u, NET_TLS_P256_SIZE);
                memcpy(g_net.tls_x509_chain_pubkey_y,
                       point + 1u + coord_len,
                       NET_TLS_P256_SIZE);
            }
        }
        g_net.tls_x509_verify_inputs =
            g_net.tls_x509_tbs_hash_alg == 1 &&
            g_net.tls_x509_signature_r_len <= NET_TLS_P256_SIZE &&
            g_net.tls_x509_signature_r_len > 0 &&
            g_net.tls_x509_signature_s_len <= NET_TLS_P256_SIZE &&
            g_net.tls_x509_signature_s_len > 0 &&
            g_net.tls_x509_chain_pubkey_alg == 2 &&
            g_net.tls_x509_chain_curve == 1 &&
            g_net.tls_x509_chain_pubkey_len == 65 ? 1 : 0;
        net_tls_compute_ecdsa_scalars();
    }
}

static void net_tls_parse_buffered_certificate(void) {
    uint32_t rx = g_net.tls_certificate_rx;
    if (rx > NET_TLS_CERT_CAPTURE_SIZE) rx = NET_TLS_CERT_CAPTURE_SIZE;

    net_tls_parse_certificate_header(tls_certificate_body,
                                     g_net.tls_certificate_len,
                                     rx);
    net_tls_parse_first_certificate_der(tls_certificate_body, rx);
    net_tls_parse_chain_certificate_der(tls_certificate_body, rx);
}

static void net_tls_capture_certificate_bytes(const uint8_t *src,
                                              uint32_t offset,
                                              uint32_t len) {
    if (!src || !len || offset >= NET_TLS_CERT_CAPTURE_SIZE) return;

    uint32_t room = NET_TLS_CERT_CAPTURE_SIZE - offset;
    uint32_t copy_len = len < room ? len : room;
    memcpy(tls_certificate_body + offset, src, copy_len);
}

static void net_tls_add_pending_bytes(uint32_t add) {
    if (!g_net.tls_pending_handshake_type ||
        g_net.tls_pending_handshake_rx >= g_net.tls_pending_handshake_len) {
        return;
    }

    uint32_t needed = g_net.tls_pending_handshake_len -
                      g_net.tls_pending_handshake_rx;
    if (add > needed) add = needed;
    g_net.tls_pending_handshake_rx += add;
    if (g_net.tls_pending_handshake_type == 0x0B) {
        g_net.tls_certificate_rx = g_net.tls_pending_handshake_rx;
    }
    if (g_net.tls_pending_handshake_rx >= g_net.tls_pending_handshake_len) {
        g_net.tls_pending_handshake_type = 0;
        g_net.tls_pending_handshake_len = 0;
        g_net.tls_pending_handshake_rx = 0;
    }
}

static void net_tls_continue_pending(const uint8_t **payload, uint16_t *length) {
    if (!g_net.tls_record_pending || !*length) return;

    uint16_t consume = *length < g_net.tls_record_pending ?
                       *length :
                       g_net.tls_record_pending;
    if (g_net.tls_pending_handshake_type == 0x0B) {
        net_tls_capture_certificate_bytes(*payload,
                                          g_net.tls_certificate_rx,
                                          consume);
    }
    net_tls_add_pending_bytes(consume);
    if (g_net.tls_certificate) net_tls_parse_buffered_certificate();
    net_tls_transcript_append_certificate();

    g_net.tls_record_pending = (uint16_t)(g_net.tls_record_pending - consume);
    *payload += consume;
    *length = (uint16_t)(*length - consume);
}

static int net_tls_certificate_complete(void) {
    return g_net.tls_certificate &&
           g_net.tls_certificate_len > 0 &&
           g_net.tls_certificate_rx >= g_net.tls_certificate_len;
}

static void net_tls_process_server_finished_record(const uint8_t *src,
                                                   uint16_t len) {
    uint8_t nonce[12];
    uint8_t aad[13];
    uint8_t plain[32];
    uint8_t digest[32];
    uint8_t expected[NET_TLS_FINISHED_SIZE];
    uint16_t cipher_len;

    if (!src || len < 8u + 16u + 16u) return;
    if (!g_net.tls_finished_tx || !g_net.tls_key_block_valid) return;

    g_net.tls_server_finished_rx = 1;
    cipher_len = (uint16_t)(len - 8u - 16u);
    if (cipher_len > sizeof(plain)) return;

    memset(nonce, 0, sizeof(nonce));
    memcpy(nonce, tls_key_block + 36, 4);
    memcpy(nonce + 4, src, 8);

    memset(aad, 0, sizeof(aad));
    aad[8] = 0x16;
    aad[9] = 0x03;
    aad[10] = 0x03;
    put16be(aad + 11, cipher_len);

    g_net.tls_server_finished_tag32 = get32be(src + 8u + cipher_len);
    if (!aes128_gcm_decrypt(tls_key_block + 16,
                            nonce,
                            aad,
                            src + 8,
                            cipher_len,
                            src + 8u + cipher_len,
                            plain)) {
        return;
    }
    g_net.tls_server_finished_decrypt = 1;
    if (cipher_len != 16 ||
        plain[0] != 0x14 ||
        plain[1] != 0 ||
        plain[2] != 0 ||
        plain[3] != NET_TLS_FINISHED_SIZE) {
        return;
    }

    sha256_hash(tls_transcript, g_net.tls_transcript_len, digest);
    net_tls_prf_sha256(tls_master_secret,
                       sizeof(tls_master_secret),
                       "server finished",
                       digest,
                       sizeof(digest),
                       expected,
                       sizeof(expected));
    g_net.tls_server_finished0 = get32be(plain + 4);
    g_net.tls_server_finished1 = get32be(plain + 8);
    g_net.tls_server_finished2 = get32be(plain + 12);
    if (memcmp(plain + 4, expected, sizeof(expected)) == 0) {
        g_net.tls_server_finished_verify = 1;
        g_net.tls_stage = 16;
    }
}

static void net_tls_process_app_data_record(const uint8_t *src,
                                            uint16_t len) {
    uint8_t nonce[12];
    uint8_t aad[13];
    uint16_t cipher_len;

    if (!src || len < 8u + 16u) return;
    if (!g_net.tls_app_tx || !g_net.tls_key_block_valid) return;

    g_net.tls_app_rx = 1;
    cipher_len = (uint16_t)(len - 8u - 16u);
    if (!cipher_len || cipher_len > sizeof(tls_app_plain)) return;

    memset(nonce, 0, sizeof(nonce));
    memcpy(nonce, tls_key_block + 36, 4);
    memcpy(nonce + 4, src, 8);

    memset(aad, 0, sizeof(aad));
    aad[7] = 1;
    aad[8] = 0x17;
    aad[9] = 0x03;
    aad[10] = 0x03;
    put16be(aad + 11, cipher_len);

    g_net.tls_app_response_tag32 = get32be(src + 8u + cipher_len);
    if (!aes128_gcm_decrypt(tls_key_block + 16,
                            nonce,
                            aad,
                            src + 8,
                            cipher_len,
                            src + 8u + cipher_len,
                            tls_app_plain)) {
        return;
    }

    g_net.tls_app_decrypt = 1;
    g_net.tls_app_response_len = cipher_len;
    net_http_capture_payload(tls_app_plain, cipher_len);
    g_net.tls_stage = 18;
    if (g_net.http_valid) g_net.http_stage = 7;
}

static void net_tls_app_pending_reset(void) {
    tls_app_pending_len = 0;
    tls_app_pending_rx = 0;
    g_net.tls_app_pending_len = 0;
    g_net.tls_app_pending_rx = 0;
}

static void net_tls_app_pending_continue(const uint8_t **payload,
                                         uint16_t *length) {
    if (!tls_app_pending_len || !*length) return;

    uint16_t needed = (uint16_t)(tls_app_pending_len - tls_app_pending_rx);
    uint16_t copy = *length < needed ? *length : needed;
    if (copy && tls_app_pending_rx + copy <= sizeof(tls_app_record)) {
        memcpy(tls_app_record + tls_app_pending_rx, *payload, copy);
        tls_app_pending_rx = (uint16_t)(tls_app_pending_rx + copy);
    }
    g_net.tls_app_pending_rx = tls_app_pending_rx;

    *payload += copy;
    *length = (uint16_t)(*length - copy);

    if (tls_app_pending_rx >= tls_app_pending_len) {
        net_tls_process_app_data_record(tls_app_record, tls_app_pending_len);
        net_tls_app_pending_reset();
    }
}

static void net_tls_app_pending_start(const uint8_t *src,
                                      uint16_t len,
                                      uint16_t available) {
    if (!src || !len || len > sizeof(tls_app_record)) return;
    if (available > len) available = len;
    memset(tls_app_record, 0, sizeof(tls_app_record));
    if (available) memcpy(tls_app_record, src, available);
    tls_app_pending_len = len;
    tls_app_pending_rx = available;
    g_net.tls_app_rx = 1;
    g_net.tls_app_pending_len = len;
    g_net.tls_app_pending_rx = available;
    if (tls_app_pending_rx >= tls_app_pending_len) {
        net_tls_process_app_data_record(tls_app_record, tls_app_pending_len);
        net_tls_app_pending_reset();
    }
}

static void net_tls_capture_payload(const uint8_t *payload, uint16_t length) {
    if (!payload || !length) return;
    net_tls_continue_pending(&payload, &length);
    net_tls_app_pending_continue(&payload, &length);
    if (length < 5) return;

    for (uint16_t record = 0; (uint16_t)(record + 5) <= length;) {
        uint8_t record_type = payload[record];
        uint16_t version = get16be(payload + record + 1);
        uint16_t record_len = get16be(payload + record + 3);
        uint16_t record_end = (uint16_t)(record + 5 + record_len);
        if (record_type < 0x14 || record_type > 0x17) return;
        if (version < 0x0301 || version > 0x0304) return;
        if (record_len == 0 || record_len > 0x4000) return;
        uint16_t parse_end = record_end <= length ? record_end : length;

        if (!g_net.tls_valid || record_type == 0x16 || record_type == 0x15) {
            g_net.tls_record_type = record_type;
            g_net.tls_record_version = version;
            g_net.tls_record_len = record_len;
            g_net.tls_handshake_type = 0;
        }
        if (record_type == 0x16 || record_type == 0x15) g_net.tls_valid = 1;

        if (g_net.tls_finished_tx && record_type == 0x14 && record_end <= length) {
            g_net.tls_server_ccs_rx = 1;
        }
        if (g_net.tls_finished_tx && record_type == 0x16 && record_end <= length) {
            net_tls_process_server_finished_record(payload + record + 5, record_len);
            record = record_end;
            continue;
        }
        if (g_net.tls_app_tx && record_type == 0x17 && record_end <= length) {
            net_tls_process_app_data_record(payload + record + 5, record_len);
            record = record_end;
            continue;
        }
        if (g_net.tls_app_tx && record_type == 0x17 && record_end > length) {
            uint16_t available = (uint16_t)(length - (record + 5));
            net_tls_app_pending_start(payload + record + 5, record_len, available);
            return;
        }

        if (record_type == 0x16) {
            uint16_t hs = (uint16_t)(record + 5);
            if (g_net.tls_pending_handshake_type &&
                g_net.tls_pending_handshake_rx < g_net.tls_pending_handshake_len) {
                uint32_t needed = g_net.tls_pending_handshake_len -
                                  g_net.tls_pending_handshake_rx;
                uint16_t available = (uint16_t)(parse_end - hs);
                uint32_t add = available < needed ? available : needed;
                if (g_net.tls_pending_handshake_type == 0x0B) {
                    net_tls_capture_certificate_bytes(payload + hs,
                                                      g_net.tls_certificate_rx,
                                                      add);
                }
                net_tls_add_pending_bytes(add);
                if (g_net.tls_certificate) net_tls_parse_buffered_certificate();
                hs = (uint16_t)(hs + add);
            }
            while ((uint16_t)(hs + 4) <= parse_end) {
                uint8_t hs_type = payload[hs];
                uint32_t hs_len = ((uint32_t)payload[hs + 1] << 16) |
                                  ((uint32_t)payload[hs + 2] << 8) |
                                  payload[hs + 3];
                uint16_t body = (uint16_t)(hs + 4);
                if (hs_type == 0x02) {
                    if ((uint32_t)body + hs_len > parse_end) break;
                    net_tls_note_handshake(hs_type, hs_len, hs_len);
                    net_tls_transcript_append(payload + hs, 4u + hs_len);
                    g_net.tls_server_hello = 1;
                    if (hs_len >= 38) {
                        uint8_t sid_len = payload[body + 34];
                        uint16_t pos = (uint16_t)(body + 35 + sid_len);
                        g_net.tls_server_version = get16be(payload + body);
                        memcpy(g_net.tls_server_random, payload + body + 2, NET_TLS_P256_SIZE);
                        g_net.tls_session_id_len = sid_len;
                        if ((uint16_t)(pos + 3) <= (uint16_t)(body + hs_len)) {
                            g_net.tls_cipher_suite = get16be(payload + pos);
                            pos = (uint16_t)(pos + 3);
                            if ((uint16_t)(pos + 2) <= (uint16_t)(body + hs_len)) {
                                g_net.tls_extensions_len = get16be(payload + pos);
                            } else {
                                g_net.tls_extensions_len = 0;
                            }
                        }
                    }
                } else if (hs_type == 0x0B) {
                    uint32_t rx = (uint32_t)(parse_end - body);
                    if (rx > hs_len) rx = hs_len;
                    net_tls_note_handshake(hs_type, hs_len, rx);
                    net_tls_capture_certificate_bytes(payload + body, 0, rx);
                    net_tls_parse_buffered_certificate();
                    net_tls_transcript_append_certificate();
                    if (rx < hs_len) break;
                } else if (hs_type == 0x0C) {
                    if ((uint32_t)body + hs_len > parse_end) {
                        uint32_t rx = (uint32_t)(parse_end - body);
                        if (rx > hs_len) rx = hs_len;
                        net_tls_note_handshake(hs_type, hs_len, rx);
                        break;
                    }
                    net_tls_note_handshake(hs_type, hs_len, hs_len);
                    net_tls_transcript_append(payload + hs, 4u + hs_len);
                    net_tls_parse_server_key_exchange(payload + body, hs_len);
                } else if ((uint32_t)body + hs_len > parse_end) {
                    uint32_t rx = (uint32_t)(parse_end - body);
                    if (rx > hs_len) rx = hs_len;
                    net_tls_note_handshake(hs_type, hs_len, rx);
                    break;
                } else {
                    net_tls_note_handshake(hs_type, hs_len, hs_len);
                    net_tls_transcript_append(payload + hs, 4u + hs_len);
                }
                hs = (uint16_t)(body + hs_len);
            }
        }

        if (record_end > length) {
            g_net.tls_record_pending = (uint16_t)(record_end - length);
            return;
        }
        record = record_end;
    }
}

static int net_handle_tcp(const uint8_t *ip,
                          uint16_t ip_payload_length,
                          uint16_t ip_header_len) {
    if (ip_payload_length < ip_header_len + 20) return 0;

    const uint8_t *tcp = ip + ip_header_len;
    uint16_t src_port = get16be(tcp + 0);
    uint16_t dst_port = get16be(tcp + 2);
    uint16_t tcp_header_len = (uint16_t)((tcp[12] >> 4) * 4u);
    if (tcp_header_len < 20 || ip_payload_length < ip_header_len + tcp_header_len) return 0;

    uint8_t flags = tcp[13];
    uint16_t tcp_payload_len = (uint16_t)(ip_payload_length - ip_header_len - tcp_header_len);
    const uint8_t *tcp_payload = tcp + tcp_header_len;

    if (src_port != g_net.tcp_target_port || dst_port != g_net.tcp_local_port) return 0;
    if (!ip_equal(ip + 12, g_net.tcp_target_ip)) return 0;

    g_net.last_tcp_src = src_port;
    g_net.last_tcp_dst = dst_port;
    g_net.last_tcp_flags = flags;
    g_net.last_tcp_seq = get32be(tcp + 4);
    g_net.last_tcp_ack = get32be(tcp + 8);

    if ((flags & (TCP_FLAG_SYN | TCP_FLAG_ACK)) == (TCP_FLAG_SYN | TCP_FLAG_ACK)) {
        g_net.tcp_synacks++;
        return 1;
    }
    if (tcp_payload_len > 0) {
        g_net.tcp_payload_rx += tcp_payload_len;
        g_net.tcp_remote_next = g_net.last_tcp_seq + tcp_payload_len;
        if (g_net.tcp_target_port == TCP_TLS_PORT) {
            net_tls_capture_payload(tcp_payload, tcp_payload_len);
        } else {
            net_http_capture_payload(tcp_payload, tcp_payload_len);
        }
        return 1;
    }
    return 0;
}

static int net_handle_ipv4(const uint8_t *frame, uint16_t length) {
    if (length < sizeof(ethernet_header_t) + 20) return 0;

    const uint8_t *ip = frame + sizeof(ethernet_header_t);
    uint16_t ip_header_len = (uint16_t)((ip[0] & 0x0Fu) * 4u);
    if ((ip[0] >> 4) != 4 || ip_header_len < 20) return 0;
    if (sizeof(ethernet_header_t) + ip_header_len > length) return 0;
    if (!ip_equal(ip + 16, g_net.local_ip)) return 0;
    uint16_t ip_total_length = get16be(ip + 2);
    if (ip_total_length < ip_header_len ||
        sizeof(ethernet_header_t) + ip_total_length > length) return 0;

    if (ip[9] == IP_PROTO_TCP) {
        return net_handle_tcp(ip, ip_total_length, ip_header_len);
    }
    if (ip[9] != IP_PROTO_UDP) return 0;
    if (sizeof(ethernet_header_t) + ip_header_len + 8 > length) return 0;

    const uint8_t *udp = ip + ip_header_len;
    uint16_t src_port = get16be(udp + 0);
    uint16_t dst_port = get16be(udp + 2);
    uint16_t udp_length = get16be(udp + 4);
    g_net.last_udp_src = src_port;
    g_net.last_udp_dst = dst_port;
    if (src_port != DNS_PORT || dst_port != DNS_SOURCE_PORT) return 0;
    if (udp_length < 8 || ip_header_len + udp_length > ip_total_length) return 0;
    return net_handle_dns(udp + 8, (uint16_t)(udp_length - 8));
}

static void net_set_http_target(const char *host, const char *path);
int net_tls_probe_url(const char *url);

static void net_poll_frames(uint32_t limit) {
    for (uint32_t i = 0; i < limit; i++) {
        uint16_t length = 0;
        if (!e1000_receive_frame(rx_frame, sizeof(rx_frame), &length)) break;
        g_net.rx_frames++;
        if (length >= sizeof(ethernet_header_t)) {
            const ethernet_header_t *eth = (const ethernet_header_t*)rx_frame;
            g_net.last_ethertype = get16be(eth->ethertype);
        }
        net_handle_arp(rx_frame, length);
        if (length >= sizeof(ethernet_header_t)) {
            const ethernet_header_t *eth = (const ethernet_header_t*)rx_frame;
            if (get16be(eth->ethertype) == ETHERTYPE_IPV4) {
                net_handle_ipv4(rx_frame, length);
            }
        }
    }
}

static void net_drain_frames(uint32_t rounds) {
    for (uint32_t i = 0; i < rounds; i++) {
        net_poll_frames(8);
    }
}

static int net_arp_probe_ip(const uint8_t target_ip[4], uint8_t *mac, uint8_t *valid) {
    const e1000_info_t *nic = e1000_info();
    uint8_t frame[60];
    ethernet_header_t *eth = (ethernet_header_t*)frame;
    arp_packet_t *arp = (arp_packet_t*)(frame + sizeof(ethernet_header_t));

    if (!nic->present || !nic->tx_ready || !nic->rx_ready || !nic->mac_valid) return 0;

    memset(frame, 0, sizeof(frame));
    for (uint32_t i = 0; i < 6; i++) eth->dst[i] = 0xFFu;
    memcpy(eth->src, nic->mac, 6);
    put16be(eth->ethertype, ETHERTYPE_ARP);

    put16be(arp->htype, ARP_HTYPE_ETHERNET);
    put16be(arp->ptype, ETHERTYPE_IPV4);
    arp->hlen = 6;
    arp->plen = 4;
    put16be(arp->oper, ARP_OP_REQUEST);
    memcpy(arp->sha, nic->mac, 6);
    memcpy(arp->spa, g_net.local_ip, 4);
    memcpy(arp->tpa, target_ip, 4);

    g_net.arp_requests++;
    if (!e1000_send_frame(frame, sizeof(frame))) return 0;

    for (uint32_t spin = 0; spin < 40000 && !*valid; spin++) {
        net_poll_frames(4);
    }
    (void)mac;
    return *valid;
}

int net_arp_probe_gateway(void) {
    return net_arp_probe_ip(g_net.gateway_ip, g_net.gateway_mac, &g_net.gateway_mac_valid);
}

static uint16_t build_dns_query(uint8_t *payload) {
    memset(payload, 0, 64);
    put16be(payload + 0, DNS_QUERY_ID);
    put16be(payload + 2, 0x0100u);
    put16be(payload + 4, 1);

    const char *host = g_net.http_host[0] ? g_net.http_host : "example.com";
    uint16_t offset = 12;
    uint16_t label_pos = offset++;
    uint8_t label_len = 0;
    for (uint16_t i = 0; host[i] && offset < 58; i++) {
        char c = host[i];
        if (c == '.') {
            if (label_len == 0 || label_len > 63) return 0;
            payload[label_pos] = label_len;
            label_pos = offset++;
            label_len = 0;
        } else {
            payload[offset++] = (uint8_t)c;
            label_len++;
        }
    }
    if (label_len == 0 || label_len > 63 || offset >= 60) return 0;
    payload[label_pos] = label_len;
    payload[offset++] = 0;
    put16be(payload + offset, 1);
    offset = (uint16_t)(offset + 2);
    put16be(payload + offset, 1);
    offset = (uint16_t)(offset + 2);
    return offset;
}

static int net_dns_query_current(void) {
    const e1000_info_t *nic = e1000_info();
    uint8_t frame[128];
    ethernet_header_t *eth = (ethernet_header_t*)frame;
    uint8_t *ip = frame + sizeof(ethernet_header_t);
    uint8_t *udp = ip + 20;
    uint8_t *dns = udp + 8;

    if (!nic->present || !nic->tx_ready || !nic->rx_ready || !nic->mac_valid) return 0;
    if (!g_net.gateway_mac_valid && !net_arp_probe_gateway()) return 0;

    memset(frame, 0, sizeof(frame));
    memcpy(eth->dst, g_net.gateway_mac, 6);
    memcpy(eth->src, nic->mac, 6);
    put16be(eth->ethertype, ETHERTYPE_IPV4);

    uint16_t dns_length = build_dns_query(dns);
    if (!dns_length) return 0;
    uint16_t udp_length = (uint16_t)(8 + dns_length);
    uint16_t ip_length = (uint16_t)(20 + udp_length);

    ip[0] = 0x45;
    ip[1] = 0;
    put16be(ip + 2, ip_length);
    put16be(ip + 4, (uint16_t)(0x1000u + (g_net.dns_queries & 0x0FFFu)));
    put16be(ip + 6, 0);
    ip[8] = 64;
    ip[9] = IP_PROTO_UDP;
    memcpy(ip + 12, g_net.local_ip, 4);
    memcpy(ip + 16, g_net.dns_ip, 4);
    put16be(ip + 10, ipv4_checksum(ip, 20));

    put16be(udp + 0, DNS_SOURCE_PORT);
    put16be(udp + 2, DNS_PORT);
    put16be(udp + 4, udp_length);
    put16be(udp + 6, 0);
    put16be(udp + 6,
            transport_checksum(g_net.local_ip, g_net.dns_ip,
                               IP_PROTO_UDP, udp, udp_length));

    g_net.dns_last_ip_valid = 0;
    g_net.dns_ip_count = 0;
    g_net.dns_ip_selected = 0;
    g_net.dns_queries++;
    if (!e1000_send_frame(frame, (uint16_t)(sizeof(ethernet_header_t) + ip_length))) return 0;

    for (uint32_t spin = 0; spin < 1000000 && !g_net.dns_last_ip_valid; spin++) {
        net_poll_frames(4);
    }
    return g_net.dns_last_ip_valid;
}

int net_dns_query_example(void) {
    net_set_http_target("example.com", "/");
    return net_dns_query_current();
}

static int net_send_tcp_segment(const uint8_t target_ip[4],
                                uint16_t target_port,
                                uint8_t flags,
                                uint32_t seq,
                                uint32_t ack,
                                const uint8_t *payload,
                                uint16_t payload_len) {
    const e1000_info_t *nic = e1000_info();
    uint8_t frame[256];
    ethernet_header_t *eth = (ethernet_header_t*)frame;
    uint8_t *ip = frame + sizeof(ethernet_header_t);
    uint8_t *tcp = ip + 20;
    uint16_t tcp_length = 20;
    uint16_t ip_length;

    if (!nic->present || !nic->tx_ready || !nic->rx_ready || !nic->mac_valid) return 0;
    if (!g_net.gateway_mac_valid && !net_arp_probe_gateway()) return 0;
    if (payload_len > 160) return 0;

    memset(frame, 0, sizeof(frame));
    memcpy(eth->dst, g_net.gateway_mac, 6);
    memcpy(eth->src, nic->mac, 6);
    put16be(eth->ethertype, ETHERTYPE_IPV4);

    ip[0] = 0x45;
    put16be(ip + 4, (uint16_t)(0x2000u + (g_net.tcp_syns & 0x0FFFu)));
    ip[8] = 64;
    ip[9] = IP_PROTO_TCP;
    memcpy(ip + 12, g_net.local_ip, 4);
    memcpy(ip + 16, target_ip, 4);

    put16be(tcp + 0, g_net.tcp_local_port);
    put16be(tcp + 2, target_port);
    put32be(tcp + 4, seq);
    put32be(tcp + 8, ack);
    tcp[13] = flags;
    put16be(tcp + 14, 8192);
    put16be(tcp + 16, 0);

    if (flags & TCP_FLAG_SYN) {
        tcp[20] = TCP_OPT_MSS;
        tcp[21] = 4;
        put16be(tcp + 22, 1460);
        tcp[24] = TCP_OPT_SACK_PERMITTED;
        tcp[25] = 2;
        tcp[26] = TCP_OPT_NOP;
        tcp[27] = TCP_OPT_WINDOW_SCALE;
        tcp[28] = 3;
        tcp[29] = 0;
        tcp[30] = TCP_OPT_NOP;
        tcp[31] = TCP_OPT_NOP;
        tcp_length = 32;
    }
    if (payload && payload_len) {
        memcpy(tcp + tcp_length, payload, payload_len);
        tcp_length = (uint16_t)(tcp_length + payload_len);
    }

    tcp[12] = (uint8_t)(((flags & TCP_FLAG_SYN) ? 8 : 5) << 4);
    ip_length = (uint16_t)(20 + tcp_length);
    put16be(ip + 2, ip_length);
    put16be(ip + 10, ipv4_checksum(ip, 20));
    put16be(tcp + 16,
            transport_checksum(g_net.local_ip, target_ip,
                               IP_PROTO_TCP, tcp, tcp_length));

    uint16_t frame_length = (uint16_t)(sizeof(ethernet_header_t) + ip_length);
    for (uint32_t attempt = 0; attempt < 8; attempt++) {
        if (e1000_send_frame(frame, frame_length)) return 1;
        net_poll_frames(2);
    }
    return 0;
}

static int net_tcp_connect_to(const uint8_t target_ip[4],
                              uint16_t target_port,
                              uint8_t send_final_ack) {
    uint32_t seq = TCP_SEQ_BASE + g_net.tcp_syns;
    uint32_t previous_synacks = g_net.tcp_synacks;

    memcpy(g_net.tcp_target_ip, target_ip, 4);
    g_net.tcp_target_port = target_port;
    g_net.tcp_local_port = (uint16_t)(TCP_SOURCE_PORT_BASE + (g_net.tcp_syns & 0x01FFu));
    g_net.last_tcp_src = 0;
    g_net.last_tcp_dst = 0;
    g_net.last_tcp_flags = 0;
    g_net.last_tcp_seq = 0;
    g_net.last_tcp_ack = 0;

    if (!g_net.gateway_mac_valid && !net_arp_probe_gateway()) {
        g_net.tcp_errors++;
        return 0;
    }

    net_drain_frames(16);
    g_net.tcp_syns++;
    if (!net_send_tcp_segment(target_ip, target_port, TCP_FLAG_SYN, seq, 0, 0, 0)) {
        g_net.tcp_errors++;
        return 0;
    }

    for (uint32_t spin = 0; spin < 1200000 && g_net.tcp_synacks == previous_synacks; spin++) {
        net_poll_frames(4);
    }

    if (g_net.tcp_synacks == previous_synacks) {
        g_net.tcp_errors++;
        return 0;
    }

    g_net.tcp_local_next = seq + 1;
    g_net.tcp_remote_next = g_net.last_tcp_seq + 1;
    if (!send_final_ack) return 1;

    if (!net_send_tcp_segment(target_ip, target_port, TCP_FLAG_ACK,
                              g_net.tcp_local_next, g_net.tcp_remote_next, 0, 0)) {
        g_net.tcp_errors++;
        return 0;
    }
    g_net.tcp_acks++;
    return 1;
}

int net_tcp_connect_example(void) {
    net_set_http_target("example.com", "/");
    g_net.dns_last_ip_valid = 0;
    if (!g_net.dns_last_ip_valid && !net_dns_query_example()) {
        g_net.tcp_errors++;
        return 0;
    }
    return net_tcp_connect_to(g_net.dns_last_ip, TCP_HTTP_PORT, 1);
}

int net_tcp_connect_dns(void) {
    return net_tcp_connect_to(g_net.dns_ip, TCP_DNS_PORT, 1);
}

static uint16_t append_str(uint8_t *dst, uint16_t offset, uint16_t max, const char *src) {
    while (*src && offset < max) {
        dst[offset++] = (uint8_t)*src++;
    }
    return offset;
}

static uint16_t host_len(const char *host) {
    uint16_t len = 0;
    while (host && host[len]) len++;
    return len;
}

static void net_set_http_target(const char *host, const char *path) {
    uint16_t i = 0;
    while (host && host[i] && (uint32_t)(i + 1) < NET_HTTP_HOST_SIZE) {
        g_net.http_host[i] = host[i];
        i++;
    }
    g_net.http_host[i] = 0;
    if (!g_net.http_host[0]) {
        memcpy(g_net.http_host, "example.com", 12);
    }

    i = 0;
    if (!path || !path[0]) path = "/";
    if (path[0] != '/' && (uint32_t)(i + 1) < NET_HTTP_PATH_SIZE) {
        g_net.http_path[i++] = '/';
    }
    for (uint16_t j = 0; path[j] && (uint32_t)(i + 1) < NET_HTTP_PATH_SIZE; j++) {
        g_net.http_path[i++] = path[j];
    }
    g_net.http_path[i] = 0;
}

static int parse_url(const char *url, char *host, uint16_t host_max,
                     char *path, uint16_t path_max) {
    if (!url || !url[0]) url = "example.com/";
    if (url[0] == 'h' && url[1] == 't' && url[2] == 't' &&
        url[3] == 'p' && (url[4] == ':' || url[4] == ';') &&
        url[5] == '/' && url[6] == '/') {
        url += 7;
    }
    if (url[0] == 'h' && url[1] == 't' && url[2] == 't' &&
        url[3] == 'p' && url[4] == 's' && (url[5] == ':' || url[5] == ';') &&
        url[6] == '/' && url[7] == '/') {
        return 0;
    }

    uint16_t hi = 0;
    while (url[hi] && url[hi] != '/' && hi + 1 < host_max) {
        if (url[hi] == ':' || url[hi] == ';') return 0;
        host[hi] = url[hi];
        hi++;
    }
    host[hi] = 0;
    if (!host[0]) return 0;

    uint16_t pi = 0;
    if (url[hi] == '/') {
        while (url[hi] && pi + 1 < path_max) {
            path[pi++] = url[hi++];
        }
    } else {
        path[pi++] = '/';
    }
    path[pi] = 0;
    return 1;
}

static int parse_tls_url(const char *url, char *host, uint16_t host_max,
                         char *path, uint16_t path_max) {
    if (!url || !url[0]) url = "example.com/";
    if (url[0] == 'h' && url[1] == 't' && url[2] == 't' &&
        url[3] == 'p' && url[4] == 's' && (url[5] == ':' || url[5] == ';') &&
        url[6] == '/' && url[7] == '/') {
        url += 8;
    } else if (url[0] == 'h' && url[1] == 't' && url[2] == 't' &&
               url[3] == 'p' && (url[4] == ':' || url[4] == ';') &&
               url[5] == '/' && url[6] == '/') {
        url += 7;
    }

    uint16_t hi = 0;
    while (url[hi] && url[hi] != '/' && hi + 1 < host_max) {
        if (url[hi] == ':' || url[hi] == ';') return 0;
        host[hi] = url[hi];
        hi++;
    }
    host[hi] = 0;
    if (!host[0]) return 0;

    uint16_t pi = 0;
    if (url[hi] == '/') {
        while (url[hi] && pi + 1 < path_max) {
            path[pi++] = url[hi++];
        }
    } else {
        path[pi++] = '/';
    }
    path[pi] = 0;
    return 1;
}

static uint16_t build_http_request(uint8_t *request, uint16_t max) {
    uint16_t offset = 0;
    offset = append_str(request, offset, max, "GET ");
    offset = append_str(request, offset, max, g_net.http_path);
    offset = append_str(request, offset, max, " HTTP/1.0\r\nHost: ");
    offset = append_str(request, offset, max, g_net.http_host);
    offset = append_str(request, offset, max, "\r\nConnection: close\r\n\r\n");
    if (offset >= max) return 0;
    request[offset] = 0;
    return offset;
}

static void put24be(uint8_t *dst, uint32_t value) {
    dst[0] = (uint8_t)((value >> 16) & 0xFFu);
    dst[1] = (uint8_t)((value >> 8) & 0xFFu);
    dst[2] = (uint8_t)(value & 0xFFu);
}

static uint16_t build_tls_client_hello(uint8_t *out, uint16_t max) {
    const char *host = g_net.http_host[0] ? g_net.http_host : "example.com";
    uint16_t hlen = host_len(host);
    uint16_t offset = 0;
    if (!hlen || hlen > 47 || max < 120) return 0;

    out[offset++] = 0x16;  /* TLS handshake record */
    out[offset++] = 0x03;
    out[offset++] = 0x01;
    uint16_t record_len_pos = offset;
    offset += 2;

    out[offset++] = 0x01;  /* ClientHello */
    uint16_t handshake_len_pos = offset;
    offset += 3;
    uint16_t body_start = offset;

    out[offset++] = 0x03;
    out[offset++] = 0x03;
    for (uint8_t i = 0; i < 32; i++) {
        uint8_t random_byte = (uint8_t)(0x4Du + i + (g_net.tcp_syns & 0x0Fu));
        g_net.tls_client_random[i] = random_byte;
        out[offset++] = random_byte;
    }
    out[offset++] = 0;     /* session id */

    put16be(out + offset, 8);
    offset = (uint16_t)(offset + 2);
    put16be(out + offset, 0xC02Fu); offset = (uint16_t)(offset + 2);
    put16be(out + offset, 0xC02Bu); offset = (uint16_t)(offset + 2);
    put16be(out + offset, 0x009Cu); offset = (uint16_t)(offset + 2);
    put16be(out + offset, 0x00FFu); offset = (uint16_t)(offset + 2);

    out[offset++] = 1;
    out[offset++] = 0;

    uint16_t extensions_len_pos = offset;
    offset += 2;
    uint16_t extensions_start = offset;

    put16be(out + offset, 0x0000u);
    offset = (uint16_t)(offset + 2);
    put16be(out + offset, (uint16_t)(5 + hlen));
    offset = (uint16_t)(offset + 2);
    put16be(out + offset, (uint16_t)(3 + hlen));
    offset = (uint16_t)(offset + 2);
    out[offset++] = 0;
    put16be(out + offset, hlen);
    offset = (uint16_t)(offset + 2);
    for (uint16_t i = 0; i < hlen; i++) out[offset++] = (uint8_t)host[i];

    put16be(out + offset, 0x000Au);
    offset = (uint16_t)(offset + 2);
    put16be(out + offset, 4);
    offset = (uint16_t)(offset + 2);
    put16be(out + offset, 2);
    offset = (uint16_t)(offset + 2);
    put16be(out + offset, 0x0017u); offset = (uint16_t)(offset + 2);

    put16be(out + offset, 0x000Bu);
    offset = (uint16_t)(offset + 2);
    put16be(out + offset, 2);
    offset = (uint16_t)(offset + 2);
    out[offset++] = 1;
    out[offset++] = 0;

    put16be(out + offset, 0x000Du);
    offset = (uint16_t)(offset + 2);
    put16be(out + offset, 8);
    offset = (uint16_t)(offset + 2);
    put16be(out + offset, 6);
    offset = (uint16_t)(offset + 2);
    put16be(out + offset, 0x0401u); offset = (uint16_t)(offset + 2);
    put16be(out + offset, 0x0501u); offset = (uint16_t)(offset + 2);
    put16be(out + offset, 0x0403u); offset = (uint16_t)(offset + 2);

    if (offset > max) return 0;
    put16be(out + extensions_len_pos, (uint16_t)(offset - extensions_start));
    put24be(out + handshake_len_pos, (uint32_t)(offset - body_start));
    put16be(out + record_len_pos, (uint16_t)(offset - 5));
    return offset;
}

static uint16_t build_tls_client_key_exchange(uint8_t *out, uint16_t max) {
    p256_point_t base;
    p256_point_t pub;
    uint8_t x[NET_TLS_P256_SIZE];
    uint8_t y[NET_TLS_P256_SIZE];
    uint16_t offset = 0;
    uint16_t record_len_pos;
    uint16_t handshake_len_pos;
    uint16_t body_start;

    if (max < 75) return 0;
    if (!p256_scalar_valid_be32(tls_client_scalar)) return 0;

    p256_base_point(&base);
    p256_point_mul_be32_projective(&pub, &base, tls_client_scalar);
    if (pub.infinity) return 0;
    p256_fe_to_be32(x, &pub.x);
    p256_fe_to_be32(y, &pub.y);

    g_net.tls_client_pubkey_valid = p256_point_from_be32(&pub, x, y);
    g_net.tls_client_pubkey_x32 = get32be(x);
    g_net.tls_client_pubkey_y32 = get32be(y);
    if (!g_net.tls_client_pubkey_valid) return 0;

    out[offset++] = 0x16;
    out[offset++] = 0x03;
    out[offset++] = 0x03;
    record_len_pos = offset;
    offset += 2;

    out[offset++] = 0x10;  /* ClientKeyExchange */
    handshake_len_pos = offset;
    offset += 3;
    body_start = offset;

    out[offset++] = 65;
    out[offset++] = 0x04;
    memcpy(out + offset, x, NET_TLS_P256_SIZE);
    offset = (uint16_t)(offset + NET_TLS_P256_SIZE);
    memcpy(out + offset, y, NET_TLS_P256_SIZE);
    offset = (uint16_t)(offset + NET_TLS_P256_SIZE);

    put24be(out + handshake_len_pos, (uint32_t)(offset - body_start));
    put16be(out + record_len_pos, (uint16_t)(offset - 5));
    return offset;
}

static int net_http_try_target(const uint8_t target_ip[4],
                               const uint8_t *request,
                               uint16_t request_len) {
    uint32_t previous_rx;

    if (!net_tcp_connect_to(target_ip, TCP_HTTP_PORT, 0)) {
        g_net.http_stage = 3;
        return 0;
    }
    g_net.http_stage = 4;

    if (!net_send_tcp_segment(g_net.tcp_target_ip,
                              g_net.tcp_target_port,
                              TCP_FLAG_PSH | TCP_FLAG_ACK,
                              g_net.tcp_local_next,
                              g_net.tcp_remote_next,
                              request,
                              request_len)) {
        g_net.tcp_payload_send_errors++;
        g_net.tcp_errors++;
        g_net.http_stage = 5;
        return 0;
    }

    uint32_t payload_end = g_net.tcp_local_next + request_len;
    g_net.tcp_payload_tx += request_len;
    g_net.tcp_local_next = payload_end;
    g_net.tcp_acks++;
    previous_rx = g_net.tcp_payload_rx;
    g_net.http_stage = 6;

    for (uint32_t spin = 0; spin < 8000000 && !g_net.http_valid; spin++) {
        net_poll_frames(4);
        if ((g_net.last_tcp_flags & TCP_FLAG_ACK) && g_net.last_tcp_ack >= payload_end) {
            g_net.tcp_payload_acked = request_len;
        }
        if (g_net.tcp_payload_rx != previous_rx && !g_net.http_valid) {
            net_send_tcp_segment(g_net.tcp_target_ip,
                                 g_net.tcp_target_port,
                                 TCP_FLAG_ACK,
                                 g_net.tcp_local_next,
                                 g_net.tcp_remote_next,
                                 0,
                                 0);
            previous_rx = g_net.tcp_payload_rx;
        }
    }

    if (g_net.http_valid) {
        net_send_tcp_segment(g_net.tcp_target_ip,
                             g_net.tcp_target_port,
                             TCP_FLAG_ACK,
                             g_net.tcp_local_next,
                             g_net.tcp_remote_next,
                             0,
                             0);
        g_net.http_stage = 7;
        return 1;
    }

    g_net.tcp_errors++;
    g_net.http_stage = g_net.tcp_payload_acked >= request_len ? 9 : 8;
    return 0;
}

static int net_tls_try_target(const uint8_t target_ip[4],
                              const uint8_t *hello,
                              uint16_t hello_len) {
    uint32_t previous_rx;
    net_tls_transcript_reset();

    if (!net_tcp_connect_to(target_ip, TCP_TLS_PORT, 1)) {
        g_net.tls_stage = 8;
        return 0;
    }
    g_net.tls_stage = 3;

    if (!net_send_tcp_segment(g_net.tcp_target_ip,
                              g_net.tcp_target_port,
                              TCP_FLAG_PSH | TCP_FLAG_ACK,
                              g_net.tcp_local_next,
                              g_net.tcp_remote_next,
                              hello,
                              hello_len)) {
        g_net.tcp_payload_send_errors++;
        g_net.tcp_errors++;
        g_net.tls_stage = 9;
        return 0;
    }

    net_tls_transcript_append(hello + 5, (uint32_t)hello_len - 5u);
    uint32_t payload_end = g_net.tcp_local_next + hello_len;
    g_net.tcp_payload_tx += hello_len;
    g_net.tcp_local_next = payload_end;
    g_net.tcp_acks++;
    g_net.tls_client_hello_len = hello_len;
    previous_rx = g_net.tcp_payload_rx;
    g_net.tls_stage = 4;

    for (uint32_t spin = 0;
         spin < 18000000 &&
         (!net_tls_certificate_complete() || !g_net.tls_server_key_exchange);
         spin++) {
        net_poll_frames(4);
        if ((g_net.last_tcp_flags & TCP_FLAG_ACK) && g_net.last_tcp_ack >= payload_end) {
            g_net.tcp_payload_acked = hello_len;
        }
        if (g_net.tcp_payload_rx != previous_rx) {
            net_send_tcp_segment(g_net.tcp_target_ip,
                                 g_net.tcp_target_port,
                                 TCP_FLAG_ACK,
                                 g_net.tcp_local_next,
                                 g_net.tcp_remote_next,
                                 0,
                                 0);
            previous_rx = g_net.tcp_payload_rx;
        }
    }

    if (g_net.tls_valid) {
        net_send_tcp_segment(g_net.tcp_target_ip,
                             g_net.tcp_target_port,
                             TCP_FLAG_ACK,
                             g_net.tcp_local_next,
                             g_net.tcp_remote_next,
                             0,
                             0);
        if (net_tls_certificate_complete() && g_net.tls_server_key_exchange) {
            g_net.tls_stage = 11;
        } else if (net_tls_certificate_complete()) {
            g_net.tls_stage = 7;
        } else {
            g_net.tls_stage = g_net.tls_certificate ? 6 : 5;
        }
        return 1;
    }

    g_net.tcp_errors++;
    g_net.tls_stage = 10;
    return 0;
}

int net_tls_send_client_key_exchange(void) {
    uint8_t record[80];
    uint16_t record_len;
    uint32_t payload_end;

    g_net.tls_client_key_exchange = 0;
    g_net.tls_client_key_exchange_acked = 0;
    g_net.tls_client_key_exchange_len = 0;
    g_net.tls_client_pubkey_valid = 0;
    g_net.tls_client_pubkey_x32 = 0;
    g_net.tls_client_pubkey_y32 = 0;

    if (!g_net.tls_server_key_exchange) return 0;
    if (!g_net.tls_kex_sig_point_done && !net_tls_verify_kex_signature()) return 0;
    if (!g_net.tls_kex_sig_match) return 0;
    if (g_net.tcp_target_port != TCP_TLS_PORT) return 0;

    record_len = build_tls_client_key_exchange(record, sizeof(record));
    if (!record_len) return 0;

    if (!net_send_tcp_segment(g_net.tcp_target_ip,
                              g_net.tcp_target_port,
                              TCP_FLAG_PSH | TCP_FLAG_ACK,
                              g_net.tcp_local_next,
                              g_net.tcp_remote_next,
                              record,
                              record_len)) {
        g_net.tcp_payload_send_errors++;
        g_net.tcp_errors++;
        g_net.tls_stage = 12;
        return 0;
    }

    net_tls_transcript_append(record + 5, (uint32_t)record_len - 5u);
    g_net.tls_handshake_hash32 = 0;
    g_net.tls_client_finished_valid = 0;
    g_net.tls_client_finished0 = 0;
    g_net.tls_client_finished1 = 0;
    g_net.tls_client_finished2 = 0;
    payload_end = g_net.tcp_local_next + record_len;
    g_net.tcp_payload_tx += record_len;
    g_net.tcp_local_next = payload_end;
    g_net.tls_client_key_exchange = 1;
    g_net.tls_client_key_exchange_len = record_len;
    g_net.tls_stage = 12;

    for (uint32_t spin = 0; spin < 4000000 && !g_net.tls_client_key_exchange_acked; spin++) {
        net_poll_frames(4);
        if ((g_net.last_tcp_flags & TCP_FLAG_ACK) &&
            g_net.last_tcp_ack >= payload_end) {
            g_net.tcp_payload_acked = record_len;
            g_net.tls_client_key_exchange_acked = 1;
        }
    }

    return g_net.tls_client_key_exchange;
}

int net_tls_derive_keys(void) {
    p256_point_t server_pub;
    p256_point_t shared;
    uint8_t seed[NET_TLS_P256_SIZE * 2u];

    g_net.tls_shared_secret_valid = 0;
    g_net.tls_shared_secret_x32 = 0;
    g_net.tls_master_secret_valid = 0;
    g_net.tls_master_secret_0 = 0;
    g_net.tls_master_secret_1 = 0;
    g_net.tls_key_block_valid = 0;
    g_net.tls_key_block_len = 0;
    g_net.tls_client_write_key32 = 0;
    g_net.tls_server_write_key32 = 0;
    g_net.tls_client_write_iv32 = 0;
    g_net.tls_server_write_iv32 = 0;
    memset(tls_shared_secret, 0, sizeof(tls_shared_secret));
    memset(tls_master_secret, 0, sizeof(tls_master_secret));
    memset(tls_key_block, 0, sizeof(tls_key_block));

    if (!g_net.tls_client_key_exchange) return 0;
    if (!g_net.tls_kex_pubkey_valid) return 0;
    if (g_net.tls_kex_params_len < 69 ||
        tls_kex_params[3] != 65 ||
        tls_kex_params[4] != 0x04) {
        return 0;
    }
    if (!p256_point_from_be32(&server_pub,
                              tls_kex_params + 5,
                              tls_kex_params + 37)) {
        return 0;
    }

    p256_point_mul_be32_projective(&shared, &server_pub, tls_client_scalar);
    if (shared.infinity) return 0;
    p256_fe_to_be32(tls_shared_secret, &shared.x);
    g_net.tls_shared_secret_x32 = get32be(tls_shared_secret);
    g_net.tls_shared_secret_valid = 1;

    memcpy(seed, g_net.tls_client_random, NET_TLS_P256_SIZE);
    memcpy(seed + NET_TLS_P256_SIZE,
           g_net.tls_server_random,
           NET_TLS_P256_SIZE);
    net_tls_prf_sha256(tls_shared_secret,
                       sizeof(tls_shared_secret),
                       "master secret",
                       seed,
                       sizeof(seed),
                       tls_master_secret,
                       sizeof(tls_master_secret));
    g_net.tls_master_secret_0 = get32be(tls_master_secret);
    g_net.tls_master_secret_1 = get32be(tls_master_secret + 4);
    g_net.tls_master_secret_valid = 1;

    memcpy(seed, g_net.tls_server_random, NET_TLS_P256_SIZE);
    memcpy(seed + NET_TLS_P256_SIZE,
           g_net.tls_client_random,
           NET_TLS_P256_SIZE);
    net_tls_prf_sha256(tls_master_secret,
                       sizeof(tls_master_secret),
                       "key expansion",
                       seed,
                       sizeof(seed),
                       tls_key_block,
                       sizeof(tls_key_block));
    g_net.tls_client_write_key32 = get32be(tls_key_block);
    g_net.tls_server_write_key32 = get32be(tls_key_block + 16);
    g_net.tls_client_write_iv32 = get32be(tls_key_block + 32);
    g_net.tls_server_write_iv32 = get32be(tls_key_block + 36);
    g_net.tls_key_block_len = sizeof(tls_key_block);
    g_net.tls_key_block_valid = 1;
    g_net.tls_stage = 13;
    return 1;
}

int net_tls_compute_finished(void) {
    uint8_t digest[32];

    g_net.tls_handshake_hash32 = 0;
    g_net.tls_client_finished_valid = 0;
    g_net.tls_client_finished0 = 0;
    g_net.tls_client_finished1 = 0;
    g_net.tls_client_finished2 = 0;

    if (!g_net.tls_key_block_valid && !net_tls_derive_keys()) return 0;
    if (!g_net.tls_master_secret_valid) return 0;
    if (!g_net.tls_transcript_len || g_net.tls_transcript_overflow) return 0;

    sha256_hash(tls_transcript, g_net.tls_transcript_len, digest);
    g_net.tls_handshake_hash32 = get32be(digest);
    net_tls_prf_sha256(tls_master_secret,
                       sizeof(tls_master_secret),
                       "client finished",
                       digest,
                       sizeof(digest),
                       tls_client_finished,
                       sizeof(tls_client_finished));
    g_net.tls_client_finished0 = get32be(tls_client_finished);
    g_net.tls_client_finished1 = get32be(tls_client_finished + 4);
    g_net.tls_client_finished2 = get32be(tls_client_finished + 8);
    g_net.tls_client_finished_valid = 1;
    g_net.tls_stage = 14;
    return 1;
}

int net_tls_send_finished(void) {
    uint8_t payload[64];
    uint8_t plain[16];
    uint8_t cipher[16];
    uint8_t tag[16];
    uint8_t nonce[12];
    uint8_t aad[13];
    uint16_t offset = 0;
    uint16_t finished_record_len;
    uint32_t payload_end;
    uint32_t previous_rx;

    g_net.tls_ccs_tx = 0;
    g_net.tls_finished_tx = 0;
    g_net.tls_finished_acked = 0;
    g_net.tls_finished_record_len = 0;
    g_net.tls_finished_tag32 = 0;

    if (!g_net.tls_client_finished_valid && !net_tls_compute_finished()) return 0;
    if (!g_net.tls_key_block_valid) return 0;

    payload[offset++] = 0x14;
    payload[offset++] = 0x03;
    payload[offset++] = 0x03;
    put16be(payload + offset, 1);
    offset = (uint16_t)(offset + 2);
    payload[offset++] = 0x01;

    plain[0] = 0x14;
    plain[1] = 0;
    plain[2] = 0;
    plain[3] = NET_TLS_FINISHED_SIZE;
    memcpy(plain + 4, tls_client_finished, NET_TLS_FINISHED_SIZE);

    memset(nonce, 0, sizeof(nonce));
    memcpy(nonce, tls_key_block + 32, 4);
    for (uint8_t i = 0; i < 8; i++) nonce[4 + i] = 0;

    memset(aad, 0, sizeof(aad));
    aad[8] = 0x16;
    aad[9] = 0x03;
    aad[10] = 0x03;
    put16be(aad + 11, sizeof(plain));

    aes128_gcm_encrypt(tls_key_block,
                       nonce,
                       aad,
                       plain,
                       sizeof(plain),
                       cipher,
                       tag);

    payload[offset++] = 0x16;
    payload[offset++] = 0x03;
    payload[offset++] = 0x03;
    finished_record_len = 8u + sizeof(cipher) + sizeof(tag);
    put16be(payload + offset, finished_record_len);
    offset = (uint16_t)(offset + 2);
    memcpy(payload + offset, nonce + 4, 8);
    offset = (uint16_t)(offset + 8);
    memcpy(payload + offset, cipher, sizeof(cipher));
    offset = (uint16_t)(offset + sizeof(cipher));
    memcpy(payload + offset, tag, sizeof(tag));
    offset = (uint16_t)(offset + sizeof(tag));

    if (!net_send_tcp_segment(g_net.tcp_target_ip,
                              g_net.tcp_target_port,
                              TCP_FLAG_PSH | TCP_FLAG_ACK,
                              g_net.tcp_local_next,
                              g_net.tcp_remote_next,
                              payload,
                              offset)) {
        g_net.tcp_payload_send_errors++;
        g_net.tcp_errors++;
        g_net.tls_stage = 15;
        return 0;
    }

    payload_end = g_net.tcp_local_next + offset;
    g_net.tcp_payload_tx += offset;
    g_net.tcp_local_next = payload_end;
    g_net.tls_ccs_tx = 1;
    g_net.tls_finished_tx = 1;
    g_net.tls_finished_record_len = finished_record_len;
    g_net.tls_finished_tag32 = get32be(tag);
    net_tls_transcript_append(plain, sizeof(plain));
    g_net.tls_stage = 15;
    previous_rx = g_net.tcp_payload_rx;

    for (uint32_t spin = 0;
         spin < 8000000 &&
         (!g_net.tls_finished_acked || !g_net.tls_server_finished_verify);
         spin++) {
        net_poll_frames(4);
        if ((g_net.last_tcp_flags & TCP_FLAG_ACK) &&
            g_net.last_tcp_ack >= payload_end) {
            g_net.tcp_payload_acked = offset;
            g_net.tls_finished_acked = 1;
        }
        if (g_net.tcp_payload_rx != previous_rx) {
            net_send_tcp_segment(g_net.tcp_target_ip,
                                 g_net.tcp_target_port,
                                 TCP_FLAG_ACK,
                                 g_net.tcp_local_next,
                                 g_net.tcp_remote_next,
                                 0,
                                 0);
            previous_rx = g_net.tcp_payload_rx;
        }
    }

    return g_net.tls_finished_tx;
}

int net_tls_send_http_get(void) {
    uint8_t request[132];
    uint8_t payload[160];
    uint8_t cipher[132];
    uint8_t tag[16];
    uint8_t nonce[12];
    uint8_t aad[13];
    uint16_t request_len;
    uint16_t offset = 0;
    uint16_t app_record_len;
    uint32_t payload_end;
    uint32_t previous_rx;

    g_net.tls_app_tx = 0;
    g_net.tls_app_acked = 0;
    g_net.tls_app_rx = 0;
    g_net.tls_app_decrypt = 0;
    g_net.tls_app_record_len = 0;
    g_net.tls_app_plain_len = 0;
    g_net.tls_app_response_len = 0;
    g_net.tls_app_pending_len = 0;
    g_net.tls_app_pending_rx = 0;
    g_net.tls_app_tag32 = 0;
    g_net.tls_app_response_tag32 = 0;
    net_tls_app_pending_reset();

    if (!g_net.tls_server_finished_verify) return 0;
    if (!g_net.tls_key_block_valid) return 0;
    if (g_net.tcp_target_port != TCP_TLS_PORT) return 0;

    request_len = build_http_request(request, sizeof(request));
    if (!request_len || request_len > sizeof(cipher)) return 0;

    memset(nonce, 0, sizeof(nonce));
    memcpy(nonce, tls_key_block + 32, 4);
    nonce[11] = 1;

    memset(aad, 0, sizeof(aad));
    aad[7] = 1;
    aad[8] = 0x17;
    aad[9] = 0x03;
    aad[10] = 0x03;
    put16be(aad + 11, request_len);

    aes128_gcm_encrypt(tls_key_block,
                       nonce,
                       aad,
                       request,
                       request_len,
                       cipher,
                       tag);

    payload[offset++] = 0x17;
    payload[offset++] = 0x03;
    payload[offset++] = 0x03;
    app_record_len = (uint16_t)(8u + request_len + sizeof(tag));
    if ((uint16_t)(5u + app_record_len) > sizeof(payload)) return 0;
    put16be(payload + offset, app_record_len);
    offset = (uint16_t)(offset + 2);
    memcpy(payload + offset, nonce + 4, 8);
    offset = (uint16_t)(offset + 8);
    memcpy(payload + offset, cipher, request_len);
    offset = (uint16_t)(offset + request_len);
    memcpy(payload + offset, tag, sizeof(tag));
    offset = (uint16_t)(offset + sizeof(tag));

    g_net.http_request_len = request_len;
    g_net.http_response_len = 0;
    g_net.http_body_offset = 0;
    g_net.http_valid = 0;
    g_net.http_status = 0;

    if (!net_send_tcp_segment(g_net.tcp_target_ip,
                              g_net.tcp_target_port,
                              TCP_FLAG_PSH | TCP_FLAG_ACK,
                              g_net.tcp_local_next,
                              g_net.tcp_remote_next,
                              payload,
                              offset)) {
        g_net.tcp_payload_send_errors++;
        g_net.tcp_errors++;
        g_net.tls_stage = 17;
        return 0;
    }

    payload_end = g_net.tcp_local_next + offset;
    g_net.tcp_payload_tx += offset;
    g_net.tcp_local_next = payload_end;
    g_net.tls_app_tx = 1;
    g_net.tls_app_record_len = app_record_len;
    g_net.tls_app_plain_len = request_len;
    g_net.tls_app_tag32 = get32be(tag);
    g_net.tls_stage = 17;
    previous_rx = g_net.tcp_payload_rx;

    for (uint32_t spin = 0;
         spin < 12000000 && (!g_net.tls_app_acked || !g_net.tls_app_decrypt);
         spin++) {
        net_poll_frames(4);
        if ((g_net.last_tcp_flags & TCP_FLAG_ACK) &&
            g_net.last_tcp_ack >= payload_end) {
            g_net.tcp_payload_acked = offset;
            g_net.tls_app_acked = 1;
        }
        if (g_net.tcp_payload_rx != previous_rx) {
            net_send_tcp_segment(g_net.tcp_target_ip,
                                 g_net.tcp_target_port,
                                 TCP_FLAG_ACK,
                                 g_net.tcp_local_next,
                                 g_net.tcp_remote_next,
                                 0,
                                 0);
            previous_rx = g_net.tcp_payload_rx;
        }
    }

    return g_net.tls_app_tx;
}

int net_http_get_example(void) {
    return net_http_get_url("example.com/");
}

int net_http_get_url(const char *url) {
    char host[NET_HTTP_HOST_SIZE];
    char path[NET_HTTP_PATH_SIZE];
    uint8_t request[160];

    if (!parse_url(url, host, sizeof(host), path, sizeof(path))) {
        g_net.http_stage = 10;
        return 0;
    }
    net_set_http_target(host, path);
    uint16_t request_len = build_http_request(request, sizeof(request));
    if (!request_len) {
        g_net.http_stage = 10;
        return 0;
    }

    g_net.http_stage = 1;
    g_net.http_request_len = request_len;
    g_net.http_response_len = 0;
    g_net.http_body_offset = 0;
    g_net.http_valid = 0;
    g_net.http_status = 0;
    g_net.tcp_payload_acked = 0;
    g_net.tcp_payload_send_errors = 0;
    g_net.dns_last_ip_valid = 0;
    g_net.dns_ip_count = 0;
    g_net.dns_ip_selected = 0;

    if (!g_net.dns_last_ip_valid) {
        for (uint8_t dns_attempt = 0; dns_attempt < 3 && !g_net.dns_last_ip_valid; dns_attempt++) {
            net_dns_query_current();
        }
        if (!g_net.dns_last_ip_valid) {
            g_net.http_stage = 2;
            return 0;
        }
    }

    uint8_t attempts = g_net.dns_ip_count ? g_net.dns_ip_count : 1;
    for (uint8_t round = 0; round < 2; round++) {
        for (uint8_t i = 0; i < attempts; i++) {
            const uint8_t *target = g_net.dns_ip_count ?
                                    g_net.dns_result_ips[i] :
                                    g_net.dns_last_ip;
            memcpy(g_net.dns_last_ip, target, 4);
            g_net.dns_ip_selected = i;
            if (net_http_try_target(target, request, request_len)) return 1;
        }
    }
    return 0;
}

int net_tls_probe_example(void) {
    return net_tls_probe_url("example.com/");
}

int net_tls_probe_url(const char *url) {
    char host[NET_HTTP_HOST_SIZE];
    char path[NET_HTTP_PATH_SIZE];
    uint8_t hello[160];

    if (!parse_tls_url(url, host, sizeof(host), path, sizeof(path))) {
        g_net.tls_stage = 6;
        g_net.tls_valid = 0;
        return 0;
    }
    net_set_http_target(host, path);

    g_net.tls_stage = 1;
    g_net.tls_valid = 0;
    g_net.tls_client_hello_len = 0;
    memset(g_net.tls_client_random, 0, sizeof(g_net.tls_client_random));
    memset(g_net.tls_server_random, 0, sizeof(g_net.tls_server_random));
    g_net.tls_record_type = 0;
    g_net.tls_record_version = 0;
    g_net.tls_record_len = 0;
    g_net.tls_handshake_type = 0;
    g_net.tls_server_hello = 0;
    g_net.tls_server_version = 0;
    g_net.tls_cipher_suite = 0;
    g_net.tls_session_id_len = 0;
    g_net.tls_extensions_len = 0;
    g_net.tls_certificate = 0;
    g_net.tls_server_key_exchange = 0;
    g_net.tls_server_key_exchange_len = 0;
    g_net.tls_kex_curve_type = 0;
    g_net.tls_kex_named_curve = 0;
    g_net.tls_kex_pubkey_len = 0;
    g_net.tls_kex_pubkey_valid = 0;
    g_net.tls_kex_pubkey_x32 = 0;
    g_net.tls_kex_pubkey_y32 = 0;
    g_net.tls_kex_sig_hash = 0;
    g_net.tls_kex_sig_alg = 0;
    g_net.tls_kex_sig_len = 0;
    g_net.tls_kex_sig_r_len = 0;
    g_net.tls_kex_sig_s_len = 0;
    g_net.tls_kex_sig_r32 = 0;
    g_net.tls_kex_sig_s32 = 0;
    memset(g_net.tls_kex_sig_r, 0, sizeof(g_net.tls_kex_sig_r));
    memset(g_net.tls_kex_sig_s, 0, sizeof(g_net.tls_kex_sig_s));
    g_net.tls_kex_params_len = 0;
    g_net.tls_kex_params_hash32 = 0;
    g_net.tls_kex_sig_verify_inputs = 0;
    g_net.tls_kex_sig_point_done = 0;
    g_net.tls_kex_sig_match = 0;
    g_net.tls_kex_sig_v32 = 0;
    g_net.tls_client_key_exchange = 0;
    g_net.tls_client_key_exchange_acked = 0;
    g_net.tls_client_key_exchange_len = 0;
    g_net.tls_client_pubkey_valid = 0;
    g_net.tls_client_pubkey_x32 = 0;
    g_net.tls_client_pubkey_y32 = 0;
    g_net.tls_shared_secret_valid = 0;
    g_net.tls_shared_secret_x32 = 0;
    g_net.tls_master_secret_valid = 0;
    g_net.tls_master_secret_0 = 0;
    g_net.tls_master_secret_1 = 0;
    g_net.tls_key_block_valid = 0;
    g_net.tls_key_block_len = 0;
    g_net.tls_client_write_key32 = 0;
    g_net.tls_server_write_key32 = 0;
    g_net.tls_client_write_iv32 = 0;
    g_net.tls_server_write_iv32 = 0;
    g_net.tls_transcript_len = 0;
    g_net.tls_transcript_overflow = 0;
    g_net.tls_handshake_hash32 = 0;
    g_net.tls_client_finished_valid = 0;
    g_net.tls_client_finished0 = 0;
    g_net.tls_client_finished1 = 0;
    g_net.tls_client_finished2 = 0;
    memset(tls_shared_secret, 0, sizeof(tls_shared_secret));
    memset(tls_master_secret, 0, sizeof(tls_master_secret));
    memset(tls_key_block, 0, sizeof(tls_key_block));
    memset(tls_kex_params, 0, sizeof(tls_kex_params));
    net_tls_transcript_reset();
    g_net.tls_certificate_len = 0;
    g_net.tls_certificate_rx = 0;
    memset(tls_certificate_body, 0, sizeof(tls_certificate_body));
    g_net.tls_certificate_list_len = 0;
    g_net.tls_first_certificate_len = 0;
    g_net.tls_second_certificate_len = 0;
    g_net.tls_first_certificate_der = 0;
    g_net.tls_first_certificate_der_header_len = 0;
    g_net.tls_first_certificate_der_len = 0;
    g_net.tls_second_certificate_der = 0;
    g_net.tls_chain_link = 0;
    g_net.tls_x509_tbs = 0;
    g_net.tls_x509_tbs_len = 0;
    g_net.tls_x509_serial_len = 0;
    g_net.tls_x509_sig_oid_len = 0;
    g_net.tls_x509_sig_alg = 0;
    g_net.tls_x509_outer_sig_oid_len = 0;
    g_net.tls_x509_outer_sig_alg = 0;
    g_net.tls_x509_signature_len = 0;
    g_net.tls_x509_signature_unused_bits = 0;
    g_net.tls_x509_signature_r_len = 0;
    g_net.tls_x509_signature_s_len = 0;
    g_net.tls_x509_signature_r32 = 0;
    g_net.tls_x509_signature_s32 = 0;
    memset(g_net.tls_x509_signature_r, 0, sizeof(g_net.tls_x509_signature_r));
    memset(g_net.tls_x509_signature_s, 0, sizeof(g_net.tls_x509_signature_s));
    g_net.tls_x509_tbs_hash_alg = 0;
    g_net.tls_x509_tbs_hash32 = 0;
    memset(g_net.tls_x509_tbs_hash, 0, sizeof(g_net.tls_x509_tbs_hash));
    g_net.tls_x509_ecdsa_scalar_inputs = 0;
    g_net.tls_x509_ecdsa_pubkey_valid = 0;
    g_net.tls_x509_ecdsa_point_done = 0;
    g_net.tls_x509_ecdsa_match = 0;
    g_net.tls_x509_ecdsa_w32 = 0;
    g_net.tls_x509_ecdsa_u1_32 = 0;
    g_net.tls_x509_ecdsa_u2_32 = 0;
    g_net.tls_x509_ecdsa_v32 = 0;
    g_net.tls_x509_validity = 0;
    g_net.tls_x509_not_before_tag = 0;
    g_net.tls_x509_not_before_len = 0;
    g_net.tls_x509_not_after_tag = 0;
    g_net.tls_x509_not_after_len = 0;
    g_net.tls_x509_not_before_date = 0;
    g_net.tls_x509_not_after_date = 0;
    g_net.tls_x509_pubkey_oid_len = 0;
    g_net.tls_x509_pubkey_alg = 0;
    g_net.tls_x509_pubkey_curve = 0;
    g_net.tls_x509_pubkey_len = 0;
    g_net.tls_x509_pubkey_x32 = 0;
    g_net.tls_x509_pubkey_y32 = 0;
    memset(g_net.tls_x509_pubkey_x, 0, sizeof(g_net.tls_x509_pubkey_x));
    memset(g_net.tls_x509_pubkey_y, 0, sizeof(g_net.tls_x509_pubkey_y));
    g_net.tls_x509_issuer_cn_len = 0;
    g_net.tls_x509_subject_cn_len = 0;
    g_net.tls_x509_issuer_cn[0] = 0;
    g_net.tls_x509_subject_cn[0] = 0;
    g_net.tls_x509_chain_subject_cn_len = 0;
    g_net.tls_x509_chain_subject_cn[0] = 0;
    g_net.tls_x509_chain_pubkey_alg = 0;
    g_net.tls_x509_chain_curve = 0;
    g_net.tls_x509_chain_pubkey_len = 0;
    g_net.tls_x509_chain_pubkey_x32 = 0;
    g_net.tls_x509_chain_pubkey_y32 = 0;
    memset(g_net.tls_x509_chain_pubkey_x, 0, sizeof(g_net.tls_x509_chain_pubkey_x));
    memset(g_net.tls_x509_chain_pubkey_y, 0, sizeof(g_net.tls_x509_chain_pubkey_y));
    g_net.tls_x509_verify_inputs = 0;
    g_net.tls_x509_known_issuer = 0;
    g_net.tls_x509_san_dns_len = 0;
    g_net.tls_x509_san_dns[0] = 0;
    g_net.tls_x509_host_match = 0;
    g_net.tls_x509_basic_constraints = 0;
    g_net.tls_x509_is_ca = 0;
    g_net.tls_x509_key_usage = 0;
    g_net.tls_x509_key_usage_bits = 0;
    g_net.tls_x509_eku = 0;
    g_net.tls_x509_eku_bits = 0;
    g_net.tls_pending_handshake_type = 0;
    g_net.tls_pending_handshake_len = 0;
    g_net.tls_pending_handshake_rx = 0;
    g_net.tls_record_pending = 0;
    g_net.http_response_len = 0;
    g_net.http_body_offset = 0;
    g_net.http_valid = 0;
    g_net.http_status = 0;
    g_net.tcp_payload_rx = 0;
    g_net.tcp_payload_tx = 0;
    g_net.tcp_payload_acked = 0;
    g_net.tcp_payload_send_errors = 0;
    g_net.dns_last_ip_valid = 0;
    g_net.dns_ip_count = 0;
    g_net.dns_ip_selected = 0;

    uint16_t hello_len = build_tls_client_hello(hello, sizeof(hello));
    if (!hello_len) {
        g_net.tls_stage = 6;
        return 0;
    }

    for (uint8_t dns_attempt = 0; dns_attempt < 3 && !g_net.dns_last_ip_valid; dns_attempt++) {
        net_dns_query_current();
    }
    if (!g_net.dns_last_ip_valid) {
        g_net.tls_stage = 4;
        return 0;
    }

    g_net.tls_stage = 2;
    uint8_t attempts = g_net.dns_ip_count ? g_net.dns_ip_count : 1;
    for (uint8_t i = 0; i < attempts; i++) {
        const uint8_t *target = g_net.dns_ip_count ?
                                g_net.dns_result_ips[i] :
                                g_net.dns_last_ip;
        memcpy(g_net.dns_last_ip, target, 4);
        g_net.dns_ip_selected = i;
        if (net_tls_try_target(target, hello, hello_len)) return 1;
    }

    return 0;
}

int net_https_get_url(const char *url) {
    if (!net_tls_probe_url(url)) return 0;
    if (!g_net.tls_kex_sig_point_done && !net_tls_verify_kex_signature()) return 0;
    if (!g_net.tls_client_key_exchange && !net_tls_send_client_key_exchange()) return 0;
    if (!g_net.tls_key_block_valid && !net_tls_derive_keys()) return 0;
    if (!g_net.tls_client_finished_valid && !net_tls_compute_finished()) return 0;
    if (!g_net.tls_server_finished_verify && !net_tls_send_finished()) return 0;
    if (!net_tls_send_http_get()) return 0;
    return g_net.tls_app_decrypt && g_net.http_response_len > 0;
}
