#include <stdint.h>
#include "p256.h"
#include "libc.h"

typedef struct {
    p256_fe_t x;
    p256_fe_t y;
    p256_fe_t z;
    uint8_t infinity;
} p256_jacobian_t;

static const p256_fe_t p256_p = {{
    0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0x00000000u,
    0x00000000u, 0x00000000u, 0x00000001u, 0xFFFFFFFFu
}};

static const p256_fe_t p256_n = {{
    0xFC632551u, 0xF3B9CAC2u, 0xA7179E84u, 0xBCE6FAADu,
    0xFFFFFFFFu, 0xFFFFFFFFu, 0x00000000u, 0xFFFFFFFFu
}};

static const uint8_t p256_n_be[32] = {
    0xFF,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x00,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xBC,0xE6,0xFA,0xAD,0xA7,0x17,0x9E,0x84,
    0xF3,0xB9,0xCA,0xC2,0xFC,0x63,0x25,0x51
};

static const uint8_t p256_n_minus_2_be[32] = {
    0xFF,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x00,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xBC,0xE6,0xFA,0xAD,0xA7,0x17,0x9E,0x84,
    0xF3,0xB9,0xCA,0xC2,0xFC,0x63,0x25,0x4F
};

static const uint8_t p256_b_be[32] = {
    0x5A,0xC6,0x35,0xD8,0xAA,0x3A,0x93,0xE7,
    0xB3,0xEB,0xBD,0x55,0x76,0x98,0x86,0xBC,
    0x65,0x1D,0x06,0xB0,0xCC,0x53,0xB0,0xF6,
    0x3B,0xCE,0x3C,0x3E,0x27,0xD2,0x60,0x4B
};

static const uint8_t p256_gx_be[32] = {
    0x6B,0x17,0xD1,0xF2,0xE1,0x2C,0x42,0x47,
    0xF8,0xBC,0xE6,0xE5,0x63,0xA4,0x40,0xF2,
    0x77,0x03,0x7D,0x81,0x2D,0xEB,0x33,0xA0,
    0xF4,0xA1,0x39,0x45,0xD8,0x98,0xC2,0x96
};

static const uint8_t p256_gy_be[32] = {
    0x4F,0xE3,0x42,0xE2,0xFE,0x1A,0x7F,0x9B,
    0x8E,0xE7,0xEB,0x4A,0x7C,0x0F,0x9E,0x16,
    0x2B,0xCE,0x33,0x57,0x6B,0x31,0x5E,0xCE,
    0xCB,0xB6,0x40,0x68,0x37,0xBF,0x51,0xF5
};

static const uint8_t p256_2gx_be[32] = {
    0x7C,0xF2,0x7B,0x18,0x8D,0x03,0x4F,0x7E,
    0x8A,0x52,0x38,0x03,0x04,0xB5,0x1A,0xC3,
    0xC0,0x89,0x69,0xE2,0x77,0xF2,0x1B,0x35,
    0xA6,0x0B,0x48,0xFC,0x47,0x66,0x99,0x78
};

static const uint8_t p256_2gy_be[32] = {
    0x07,0x77,0x55,0x10,0xDB,0x8E,0xD0,0x40,
    0x29,0x3D,0x9A,0xC6,0x9F,0x74,0x30,0xDB,
    0xBA,0x7D,0xAD,0xE6,0x3C,0xE9,0x82,0x29,
    0x9E,0x04,0xB7,0x9D,0x22,0x78,0x73,0xD1
};

static const uint8_t p256_3gx_be[32] = {
    0x5E,0xCB,0xE4,0xD1,0xA6,0x33,0x0A,0x44,
    0xC8,0xF7,0xEF,0x95,0x1D,0x4B,0xF1,0x65,
    0xE6,0xC6,0xB7,0x21,0xEF,0xAD,0xA9,0x85,
    0xFB,0x41,0x66,0x1B,0xC6,0xE7,0xFD,0x6C
};

static const uint8_t p256_3gy_be[32] = {
    0x87,0x34,0x64,0x0C,0x49,0x98,0xFF,0x7E,
    0x37,0x4B,0x06,0xCE,0x1A,0x64,0xA2,0xEC,
    0xD8,0x2A,0xB0,0x36,0x38,0x4F,0xB8,0x3D,
    0x9A,0x79,0xB1,0x27,0xA2,0x7D,0x50,0x32
};

static uint32_t be32(const uint8_t *src) {
    return ((uint32_t)src[0] << 24) |
           ((uint32_t)src[1] << 16) |
           ((uint32_t)src[2] << 8) |
           src[3];
}

void p256_fe_zero(p256_fe_t *out) {
    memset(out, 0, sizeof(*out));
}

void p256_fe_one(p256_fe_t *out) {
    p256_fe_zero(out);
    out->v[0] = 1;
}

void p256_fe_from_be32(p256_fe_t *out, const uint8_t src[32]) {
    for (uint8_t i = 0; i < P256_LIMBS; i++) {
        out->v[i] = be32(src + (uint32_t)(P256_LIMBS - 1u - i) * 4u);
    }
}

void p256_fe_to_be32(uint8_t dst[32], const p256_fe_t *src) {
    for (uint8_t i = 0; i < P256_LIMBS; i++) {
        uint32_t value = src->v[P256_LIMBS - 1u - i];
        dst[(uint32_t)i * 4u] = (uint8_t)(value >> 24);
        dst[(uint32_t)i * 4u + 1u] = (uint8_t)(value >> 16);
        dst[(uint32_t)i * 4u + 2u] = (uint8_t)(value >> 8);
        dst[(uint32_t)i * 4u + 3u] = (uint8_t)value;
    }
}

static int p256_cmp(const p256_fe_t *a, const p256_fe_t *b) {
    for (int i = (int)P256_LIMBS - 1; i >= 0; i--) {
        if (a->v[i] > b->v[i]) return 1;
        if (a->v[i] < b->v[i]) return -1;
    }
    return 0;
}

static uint32_t p256_raw_sub(p256_fe_t *out, const p256_fe_t *a, const p256_fe_t *b) {
    uint32_t borrow = 0;
    for (uint8_t i = 0; i < P256_LIMBS; i++) {
        uint64_t av = a->v[i];
        uint64_t bv = (uint64_t)b->v[i] + borrow;
        out->v[i] = (uint32_t)(av - bv);
        borrow = av < bv ? 1u : 0u;
    }
    return borrow;
}

static uint32_t p256_raw_add(p256_fe_t *out, const p256_fe_t *a, const p256_fe_t *b) {
    uint64_t carry = 0;
    for (uint8_t i = 0; i < P256_LIMBS; i++) {
        uint64_t sum = (uint64_t)a->v[i] + b->v[i] + carry;
        out->v[i] = (uint32_t)sum;
        carry = sum >> 32;
    }
    return (uint32_t)carry;
}

static void p256_reduce_once(p256_fe_t *out, uint32_t carry) {
    if (carry || p256_cmp(out, &p256_p) >= 0) {
        p256_fe_t tmp;
        p256_raw_sub(&tmp, out, &p256_p);
        *out = tmp;
    }
}

void p256_fe_add(p256_fe_t *out, const p256_fe_t *a, const p256_fe_t *b) {
    uint32_t carry = p256_raw_add(out, a, b);
    p256_reduce_once(out, carry);
}

void p256_fe_sub(p256_fe_t *out, const p256_fe_t *a, const p256_fe_t *b) {
    if (p256_raw_sub(out, a, b)) {
        p256_fe_t tmp;
        p256_raw_add(&tmp, out, &p256_p);
        *out = tmp;
    }
}

static uint8_t get_bit512(const uint32_t value[16], uint16_t bit) {
    return (uint8_t)((value[bit / 32u] >> (bit % 32u)) & 1u);
}

static void rem_shift_in(uint32_t rem[9], uint8_t bit) {
    uint32_t carry = bit;
    for (uint8_t i = 0; i < 9; i++) {
        uint32_t next = rem[i] >> 31;
        rem[i] = (rem[i] << 1) | carry;
        carry = next;
    }
}

static int rem_cmp_mod(const uint32_t rem[9], const p256_fe_t *mod) {
    if (rem[8]) return 1;
    for (int i = 7; i >= 0; i--) {
        if (rem[i] > mod->v[i]) return 1;
        if (rem[i] < mod->v[i]) return -1;
    }
    return 0;
}

static void rem_sub_mod(uint32_t rem[9], const p256_fe_t *mod) {
    uint32_t borrow = 0;
    for (uint8_t i = 0; i < P256_LIMBS; i++) {
        uint64_t av = rem[i];
        uint64_t bv = (uint64_t)mod->v[i] + borrow;
        rem[i] = (uint32_t)(av - bv);
        borrow = av < bv ? 1u : 0u;
    }
    rem[8] -= borrow;
}

static void p256_reduce_product_mod(p256_fe_t *out,
                                    const uint32_t product[16],
                                    const p256_fe_t *mod) {
    uint32_t rem[9];
    memset(rem, 0, sizeof(rem));

    for (int bit = 511; bit >= 0; bit--) {
        rem_shift_in(rem, get_bit512(product, (uint16_t)bit));
        if (rem_cmp_mod(rem, mod) >= 0) rem_sub_mod(rem, mod);
    }

    for (uint8_t i = 0; i < P256_LIMBS; i++) {
        out->v[i] = rem[i];
    }
}

static void p256_reduce_product(p256_fe_t *out, const uint32_t product[16]) {
    p256_reduce_product_mod(out, product, &p256_p);
}

void p256_fe_mul(p256_fe_t *out, const p256_fe_t *a, const p256_fe_t *b) {
    uint32_t product[16];
    memset(product, 0, sizeof(product));

    for (uint8_t i = 0; i < P256_LIMBS; i++) {
        uint64_t carry = 0;
        for (uint8_t j = 0; j < P256_LIMBS; j++) {
            uint8_t k = (uint8_t)(i + j);
            uint64_t cur = (uint64_t)a->v[i] * b->v[j] + product[k] + carry;
            product[k] = (uint32_t)cur;
            carry = cur >> 32;
        }
        for (uint8_t k = (uint8_t)(i + P256_LIMBS); carry && k < 16; k++) {
            uint64_t cur = (uint64_t)product[k] + carry;
            product[k] = (uint32_t)cur;
            carry = cur >> 32;
        }
    }

    p256_reduce_product(out, product);
}

static uint8_t p256_exp_p_minus_2_bit(uint16_t bit) {
    static const uint8_t exp[32] = {
        0xFF,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x01,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0xFF,0xFF,0xFF,0xFF,
        0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFD
    };
    uint16_t byte = (uint16_t)(31u - bit / 8u);
    uint8_t shift = (uint8_t)(bit % 8u);
    return (uint8_t)((exp[byte] >> shift) & 1u);
}

void p256_fe_inv(p256_fe_t *out, const p256_fe_t *a) {
    p256_fe_t result;
    p256_fe_t base = *a;
    p256_fe_one(&result);

    for (int bit = 255; bit >= 0; bit--) {
        p256_fe_mul(&result, &result, &result);
        if (p256_exp_p_minus_2_bit((uint16_t)bit)) {
            p256_fe_mul(&result, &result, &base);
        }
    }

    *out = result;
}

static uint8_t p256_fe_equal(const p256_fe_t *a, const p256_fe_t *b) {
    return p256_cmp(a, b) == 0;
}

uint8_t p256_fe_equal_public(const p256_fe_t *a, const p256_fe_t *b) {
    return p256_fe_equal(a, b);
}

static void p256_fe_square(p256_fe_t *out, const p256_fe_t *a) {
    p256_fe_mul(out, a, a);
}

static void p256_scalar_reduce_once(uint32_t value[9]) {
    if (rem_cmp_mod(value, &p256_n) >= 0) rem_sub_mod(value, &p256_n);
}

void p256_scalar_from_be32(p256_fe_t *out, const uint8_t src[32]) {
    p256_fe_from_be32(out, src);
    while (p256_cmp(out, &p256_n) >= 0) {
        p256_raw_sub(out, out, &p256_n);
    }
}

uint8_t p256_scalar_valid_be32(const uint8_t src[32]) {
    p256_fe_t value;
    p256_fe_from_be32(&value, src);
    return !p256_fe_is_zero(&value) && p256_cmp(&value, &p256_n) < 0;
}

void p256_scalar_from_fe(p256_fe_t *out, const p256_fe_t *src) {
    *out = *src;
    while (p256_cmp(out, &p256_n) >= 0) {
        p256_raw_sub(out, out, &p256_n);
    }
}

static uint8_t p256_fe_valid_be32(const uint8_t src[32]) {
    p256_fe_t value;
    p256_fe_from_be32(&value, src);
    return p256_cmp(&value, &p256_p) < 0;
}

void p256_scalar_add(p256_fe_t *out, const p256_fe_t *a, const p256_fe_t *b) {
    uint32_t sum[9];
    uint64_t carry = 0;
    for (uint8_t i = 0; i < P256_LIMBS; i++) {
        uint64_t v = (uint64_t)a->v[i] + b->v[i] + carry;
        sum[i] = (uint32_t)v;
        carry = v >> 32;
    }
    sum[8] = (uint32_t)carry;
    p256_scalar_reduce_once(sum);

    for (uint8_t i = 0; i < P256_LIMBS; i++) out->v[i] = sum[i];
}

void p256_scalar_mul(p256_fe_t *out, const p256_fe_t *a, const p256_fe_t *b) {
    uint32_t product[16];
    memset(product, 0, sizeof(product));

    for (uint8_t i = 0; i < P256_LIMBS; i++) {
        uint64_t carry = 0;
        for (uint8_t j = 0; j < P256_LIMBS; j++) {
            uint8_t k = (uint8_t)(i + j);
            uint64_t cur = (uint64_t)a->v[i] * b->v[j] + product[k] + carry;
            product[k] = (uint32_t)cur;
            carry = cur >> 32;
        }
        for (uint8_t k = (uint8_t)(i + P256_LIMBS); carry && k < 16; k++) {
            uint64_t cur = (uint64_t)product[k] + carry;
            product[k] = (uint32_t)cur;
            carry = cur >> 32;
        }
    }

    p256_reduce_product_mod(out, product, &p256_n);
}

static uint8_t p256_n_minus_2_bit(uint16_t bit) {
    uint16_t byte = (uint16_t)(31u - bit / 8u);
    uint8_t shift = (uint8_t)(bit % 8u);
    return (uint8_t)((p256_n_minus_2_be[byte] >> shift) & 1u);
}

void p256_scalar_inv(p256_fe_t *out, const p256_fe_t *a) {
    p256_fe_t result;
    p256_fe_t base = *a;
    p256_fe_one(&result);

    for (int bit = 255; bit >= 0; bit--) {
        p256_scalar_mul(&result, &result, &result);
        if (p256_n_minus_2_bit((uint16_t)bit)) {
            p256_scalar_mul(&result, &result, &base);
        }
    }

    *out = result;
}

static void p256_fe_sub_small(p256_fe_t *out, const p256_fe_t *a, uint32_t value) {
    p256_fe_t small;
    p256_fe_zero(&small);
    small.v[0] = value;
    p256_fe_sub(out, a, &small);
}

static void p256_point_infinity(p256_point_t *out) {
    p256_fe_zero(&out->x);
    p256_fe_zero(&out->y);
    out->infinity = 1;
}

static uint8_t p256_point_on_curve(const p256_point_t *p) {
    if (p->infinity) return 1;

    p256_fe_t y2;
    p256_fe_t x2;
    p256_fe_t x3;
    p256_fe_t rhs;
    p256_fe_t b;

    p256_fe_square(&y2, &p->y);
    p256_fe_square(&x2, &p->x);
    p256_fe_mul(&x3, &x2, &p->x);

    p256_fe_sub(&rhs, &x3, &p->x);
    p256_fe_sub(&rhs, &rhs, &p->x);
    p256_fe_sub(&rhs, &rhs, &p->x);
    p256_fe_from_be32(&b, p256_b_be);
    p256_fe_add(&rhs, &rhs, &b);

    return p256_fe_equal(&y2, &rhs);
}

uint8_t p256_point_from_be32(p256_point_t *out,
                             const uint8_t x[32],
                             const uint8_t y[32]) {
    if (!p256_fe_valid_be32(x) || !p256_fe_valid_be32(y)) {
        p256_point_infinity(out);
        return 0;
    }

    p256_fe_from_be32(&out->x, x);
    p256_fe_from_be32(&out->y, y);
    out->infinity = 0;
    if (!p256_point_on_curve(out)) {
        p256_point_infinity(out);
        return 0;
    }

    return 1;
}

uint8_t p256_point_is_on_curve(const p256_point_t *p) {
    return p256_point_on_curve(p);
}

void p256_base_point(p256_point_t *out) {
    p256_fe_from_be32(&out->x, p256_gx_be);
    p256_fe_from_be32(&out->y, p256_gy_be);
    out->infinity = 0;
}

static void p256_fe_twice(p256_fe_t *out, const p256_fe_t *a) {
    p256_fe_add(out, a, a);
}

static void p256_fe_thrice(p256_fe_t *out, const p256_fe_t *a) {
    p256_fe_t tmp;
    p256_fe_add(&tmp, a, a);
    p256_fe_add(out, &tmp, a);
}

static void p256_fe_four(p256_fe_t *out, const p256_fe_t *a) {
    p256_fe_t tmp;
    p256_fe_twice(&tmp, a);
    p256_fe_twice(out, &tmp);
}

static void p256_fe_eight(p256_fe_t *out, const p256_fe_t *a) {
    p256_fe_t tmp;
    p256_fe_four(&tmp, a);
    p256_fe_twice(out, &tmp);
}

static void p256_jacobian_infinity(p256_jacobian_t *out) {
    p256_fe_zero(&out->x);
    p256_fe_zero(&out->y);
    p256_fe_zero(&out->z);
    out->infinity = 1;
}

static void p256_jacobian_from_point(p256_jacobian_t *out, const p256_point_t *p) {
    if (p->infinity) {
        p256_jacobian_infinity(out);
        return;
    }
    out->x = p->x;
    out->y = p->y;
    p256_fe_one(&out->z);
    out->infinity = 0;
}

static void p256_jacobian_to_point(p256_point_t *out, const p256_jacobian_t *p) {
    if (p->infinity || p256_fe_is_zero(&p->z)) {
        p256_point_infinity(out);
        return;
    }

    p256_fe_t zinv;
    p256_fe_t z2;
    p256_fe_t z3;

    p256_fe_inv(&zinv, &p->z);
    p256_fe_square(&z2, &zinv);
    p256_fe_mul(&z3, &z2, &zinv);
    p256_fe_mul(&out->x, &p->x, &z2);
    p256_fe_mul(&out->y, &p->y, &z3);
    out->infinity = 0;
}

static void p256_jacobian_double(p256_jacobian_t *out, const p256_jacobian_t *p) {
    if (p->infinity || p256_fe_is_zero(&p->y)) {
        p256_jacobian_infinity(out);
        return;
    }

    p256_fe_t delta;
    p256_fe_t gamma;
    p256_fe_t beta;
    p256_fe_t alpha;
    p256_fe_t alpha2;
    p256_fe_t x_minus_delta;
    p256_fe_t x_plus_delta;
    p256_fe_t four_beta;
    p256_fe_t eight_beta;
    p256_fe_t gamma2;
    p256_fe_t eight_gamma2;
    p256_fe_t y_plus_z;
    p256_fe_t x3;
    p256_fe_t y3;
    p256_fe_t z3;
    p256_fe_t tmp;

    p256_fe_square(&delta, &p->z);
    p256_fe_square(&gamma, &p->y);
    p256_fe_mul(&beta, &p->x, &gamma);

    p256_fe_sub(&x_minus_delta, &p->x, &delta);
    p256_fe_add(&x_plus_delta, &p->x, &delta);
    p256_fe_mul(&alpha, &x_minus_delta, &x_plus_delta);
    p256_fe_thrice(&alpha, &alpha);

    p256_fe_square(&alpha2, &alpha);
    p256_fe_eight(&eight_beta, &beta);
    p256_fe_sub(&x3, &alpha2, &eight_beta);

    p256_fe_four(&four_beta, &beta);
    p256_fe_sub(&tmp, &four_beta, &x3);
    p256_fe_mul(&tmp, &alpha, &tmp);
    p256_fe_square(&gamma2, &gamma);
    p256_fe_eight(&eight_gamma2, &gamma2);
    p256_fe_sub(&y3, &tmp, &eight_gamma2);

    p256_fe_add(&y_plus_z, &p->y, &p->z);
    p256_fe_square(&z3, &y_plus_z);
    p256_fe_sub(&z3, &z3, &gamma);
    p256_fe_sub(&z3, &z3, &delta);

    out->x = x3;
    out->y = y3;
    out->z = z3;
    out->infinity = 0;
}

static void p256_jacobian_add_mixed(p256_jacobian_t *out,
                                    const p256_jacobian_t *a,
                                    const p256_point_t *b) {
    if (a->infinity) {
        p256_jacobian_from_point(out, b);
        return;
    }
    if (b->infinity) {
        *out = *a;
        return;
    }

    p256_fe_t z1z1;
    p256_fe_t u2;
    p256_fe_t s2;
    p256_fe_t h;
    p256_fe_t hh;
    p256_fe_t i;
    p256_fe_t j;
    p256_fe_t r;
    p256_fe_t v;
    p256_fe_t x3;
    p256_fe_t y3;
    p256_fe_t z3;
    p256_fe_t tmp;
    p256_fe_t tmp2;

    p256_fe_square(&z1z1, &a->z);
    p256_fe_mul(&u2, &b->x, &z1z1);
    p256_fe_mul(&s2, &a->z, &z1z1);
    p256_fe_mul(&s2, &s2, &b->y);
    p256_fe_sub(&h, &u2, &a->x);
    p256_fe_sub(&r, &s2, &a->y);

    if (p256_fe_is_zero(&h)) {
        if (p256_fe_is_zero(&r)) {
            p256_jacobian_double(out, a);
        } else {
            p256_jacobian_infinity(out);
        }
        return;
    }

    p256_fe_twice(&r, &r);
    p256_fe_square(&hh, &h);
    p256_fe_four(&i, &hh);
    p256_fe_mul(&j, &h, &i);
    p256_fe_mul(&v, &a->x, &i);

    p256_fe_square(&x3, &r);
    p256_fe_sub(&x3, &x3, &j);
    p256_fe_twice(&tmp, &v);
    p256_fe_sub(&x3, &x3, &tmp);

    p256_fe_sub(&tmp, &v, &x3);
    p256_fe_mul(&tmp, &r, &tmp);
    p256_fe_mul(&tmp2, &a->y, &j);
    p256_fe_twice(&tmp2, &tmp2);
    p256_fe_sub(&y3, &tmp, &tmp2);

    p256_fe_add(&z3, &a->z, &h);
    p256_fe_square(&z3, &z3);
    p256_fe_sub(&z3, &z3, &z1z1);
    p256_fe_sub(&z3, &z3, &hh);

    out->x = x3;
    out->y = y3;
    out->z = z3;
    out->infinity = 0;
}

void p256_point_double(p256_point_t *out, const p256_point_t *p) {
    if (p->infinity || p256_fe_is_zero(&p->y)) {
        *out = *p;
        out->infinity = 1;
        return;
    }

    p256_fe_t x2;
    p256_fe_t num;
    p256_fe_t den;
    p256_fe_t inv;
    p256_fe_t lambda;
    p256_fe_t lambda2;
    p256_fe_t two_x;
    p256_fe_t x3;
    p256_fe_t y3;
    p256_fe_t tmp;

    p256_fe_square(&x2, &p->x);
    p256_fe_add(&num, &x2, &x2);
    p256_fe_add(&num, &num, &x2);
    p256_fe_sub_small(&num, &num, 3);

    p256_fe_add(&den, &p->y, &p->y);
    p256_fe_inv(&inv, &den);
    p256_fe_mul(&lambda, &num, &inv);

    p256_fe_square(&lambda2, &lambda);
    p256_fe_add(&two_x, &p->x, &p->x);
    p256_fe_sub(&x3, &lambda2, &two_x);

    p256_fe_sub(&tmp, &p->x, &x3);
    p256_fe_mul(&tmp, &lambda, &tmp);
    p256_fe_sub(&y3, &tmp, &p->y);

    out->x = x3;
    out->y = y3;
    out->infinity = 0;
}

void p256_point_add(p256_point_t *out, const p256_point_t *a, const p256_point_t *b) {
    if (a->infinity) {
        *out = *b;
        return;
    }
    if (b->infinity) {
        *out = *a;
        return;
    }
    if (p256_fe_equal(&a->x, &b->x)) {
        if (p256_fe_equal(&a->y, &b->y)) {
            p256_point_double(out, a);
        } else {
            *out = *a;
            out->infinity = 1;
        }
        return;
    }

    p256_fe_t dy;
    p256_fe_t dx;
    p256_fe_t inv;
    p256_fe_t lambda;
    p256_fe_t lambda2;
    p256_fe_t x3;
    p256_fe_t y3;
    p256_fe_t tmp;

    p256_fe_sub(&dy, &b->y, &a->y);
    p256_fe_sub(&dx, &b->x, &a->x);
    p256_fe_inv(&inv, &dx);
    p256_fe_mul(&lambda, &dy, &inv);

    p256_fe_square(&lambda2, &lambda);
    p256_fe_sub(&x3, &lambda2, &a->x);
    p256_fe_sub(&x3, &x3, &b->x);

    p256_fe_sub(&tmp, &a->x, &x3);
    p256_fe_mul(&tmp, &lambda, &tmp);
    p256_fe_sub(&y3, &tmp, &a->y);

    out->x = x3;
    out->y = y3;
    out->infinity = 0;
}

void p256_point_mul_u32(p256_point_t *out, const p256_point_t *p, uint32_t scalar) {
    p256_point_t acc;
    p256_point_infinity(&acc);

    for (int bit = 31; bit >= 0; bit--) {
        p256_point_double(&acc, &acc);
        if ((scalar >> bit) & 1u) {
            p256_point_add(&acc, &acc, p);
        }
    }

    *out = acc;
}

void p256_point_mul_be32(p256_point_t *out, const p256_point_t *p, const uint8_t scalar[32]) {
    p256_point_t acc;
    p256_point_infinity(&acc);

    for (uint16_t i = 0; i < 32u; i++) {
        uint8_t byte = scalar[i];
        for (int bit = 7; bit >= 0; bit--) {
            p256_point_double(&acc, &acc);
            if ((byte >> bit) & 1u) {
                p256_point_add(&acc, &acc, p);
            }
        }
    }

    *out = acc;
}

void p256_point_mul_be32_projective(p256_point_t *out,
                                    const p256_point_t *p,
                                    const uint8_t scalar[32]) {
    p256_jacobian_t acc;
    p256_jacobian_infinity(&acc);

    for (uint16_t i = 0; i < 32u; i++) {
        uint8_t byte = scalar[i];
        for (int bit = 7; bit >= 0; bit--) {
            p256_jacobian_double(&acc, &acc);
            if ((byte >> bit) & 1u) {
                p256_jacobian_add_mixed(&acc, &acc, p);
            }
        }
    }

    p256_jacobian_to_point(out, &acc);
}

uint32_t p256_fe_prefix32(const p256_fe_t *a) {
    return a->v[7];
}

uint8_t p256_fe_is_zero(const p256_fe_t *a) {
    uint32_t value = 0;
    for (uint8_t i = 0; i < P256_LIMBS; i++) value |= a->v[i];
    return value == 0;
}

uint32_t p256_selftest(void) {
    p256_fe_t zero;
    p256_fe_t one;
    p256_fe_t two;
    p256_fe_t three;
    p256_fe_t out;
    p256_fe_t expect;
    p256_point_t g;
    p256_point_t doubled;
    p256_point_t tripled;
    p256_point_t scaled;
    p256_point_t scaled_be;
    p256_point_t projected;
    p256_point_t zero_scaled;
    p256_fe_t expect_x;
    p256_fe_t expect_y;
    p256_fe_t n_minus_one;
    p256_fe_t n_minus_two;
    p256_fe_t scalar_a;
    p256_fe_t scalar_b;
    uint8_t scalar_three[32];
    uint8_t scalar_zero[32];

    p256_fe_zero(&zero);
    p256_fe_one(&one);
    p256_fe_add(&two, &one, &one);
    p256_fe_add(&three, &two, &one);

    p256_fe_add(&out, &p256_p, &one);
    uint32_t flags = p256_cmp(&out, &one) == 0 ? 1u : 0u;

    p256_fe_sub(&out, &zero, &one);
    flags |= p256_cmp(&out, &p256_p) < 0 && out.v[0] == 0xFFFFFFFEu ? 2u : 0u;

    p256_fe_mul(&out, &three, &three);
    p256_fe_zero(&expect);
    expect.v[0] = 9;
    flags |= p256_cmp(&out, &expect) == 0 ? 4u : 0u;

    p256_fe_mul(&out, &p256_p, &three);
    flags |= p256_fe_is_zero(&out) ? 8u : 0u;

    p256_fe_inv(&out, &three);
    p256_fe_mul(&out, &out, &three);
    flags |= p256_cmp(&out, &one) == 0 ? 16u : 0u;

    p256_fe_from_be32(&g.x, p256_gx_be);
    p256_fe_from_be32(&g.y, p256_gy_be);
    g.infinity = 0;
    flags |= p256_point_on_curve(&g) ? 32u : 0u;

    p256_point_double(&doubled, &g);
    p256_fe_from_be32(&expect_x, p256_2gx_be);
    p256_fe_from_be32(&expect_y, p256_2gy_be);
    flags |= !doubled.infinity &&
             p256_fe_equal(&doubled.x, &expect_x) &&
             p256_fe_equal(&doubled.y, &expect_y) ? 64u : 0u;

    p256_point_add(&tripled, &doubled, &g);
    p256_fe_from_be32(&expect_x, p256_3gx_be);
    p256_fe_from_be32(&expect_y, p256_3gy_be);
    flags |= !tripled.infinity &&
             p256_fe_equal(&tripled.x, &expect_x) &&
             p256_fe_equal(&tripled.y, &expect_y) ? 128u : 0u;

    p256_point_mul_u32(&scaled, &g, 3u);
    flags |= !scaled.infinity &&
             p256_fe_equal(&scaled.x, &expect_x) &&
             p256_fe_equal(&scaled.y, &expect_y) ? 256u : 0u;

    memset(scalar_three, 0, sizeof(scalar_three));
    memset(scalar_zero, 0, sizeof(scalar_zero));
    scalar_three[31] = 3u;
    p256_point_mul_be32(&scaled_be, &g, scalar_three);
    flags |= !scaled_be.infinity &&
             p256_fe_equal(&scaled_be.x, &expect_x) &&
             p256_fe_equal(&scaled_be.y, &expect_y) ? 512u : 0u;

    p256_point_mul_be32_projective(&projected, &g, scalar_three);
    flags |= !projected.infinity &&
             p256_fe_equal(&projected.x, &expect_x) &&
             p256_fe_equal(&projected.y, &expect_y) ? 32768u : 0u;

    p256_point_mul_be32(&zero_scaled, &g, scalar_zero);
    flags |= zero_scaled.infinity ? 1024u : 0u;

    p256_scalar_from_be32(&out, p256_n_be);
    flags |= p256_fe_is_zero(&out) ? 2048u : 0u;

    p256_raw_sub(&n_minus_one, &p256_n, &one);
    p256_raw_sub(&n_minus_two, &n_minus_one, &one);
    p256_scalar_add(&out, &n_minus_one, &two);
    flags |= p256_cmp(&out, &one) == 0 ? 4096u : 0u;

    p256_scalar_mul(&out, &n_minus_one, &two);
    flags |= p256_cmp(&out, &n_minus_two) == 0 ? 8192u : 0u;

    p256_scalar_inv(&scalar_a, &three);
    p256_scalar_mul(&scalar_b, &scalar_a, &three);
    flags |= p256_cmp(&scalar_b, &one) == 0 ? 16384u : 0u;

    return flags;
}
