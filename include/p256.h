#ifndef MINERVA_P256_H
#define MINERVA_P256_H

#include <stdint.h>

#define P256_LIMBS 8u

typedef struct {
    uint32_t v[P256_LIMBS];
} p256_fe_t;

typedef struct {
    p256_fe_t x;
    p256_fe_t y;
    uint8_t infinity;
} p256_point_t;

void p256_fe_zero(p256_fe_t *out);
void p256_fe_one(p256_fe_t *out);
void p256_fe_from_be32(p256_fe_t *out, const uint8_t src[32]);
void p256_fe_to_be32(uint8_t dst[32], const p256_fe_t *src);
void p256_fe_add(p256_fe_t *out, const p256_fe_t *a, const p256_fe_t *b);
void p256_fe_sub(p256_fe_t *out, const p256_fe_t *a, const p256_fe_t *b);
void p256_fe_mul(p256_fe_t *out, const p256_fe_t *a, const p256_fe_t *b);
void p256_fe_inv(p256_fe_t *out, const p256_fe_t *a);
uint32_t p256_fe_prefix32(const p256_fe_t *a);
uint8_t p256_fe_is_zero(const p256_fe_t *a);
uint8_t p256_fe_equal_public(const p256_fe_t *a, const p256_fe_t *b);
void p256_scalar_from_fe(p256_fe_t *out, const p256_fe_t *src);
void p256_scalar_from_be32(p256_fe_t *out, const uint8_t src[32]);
uint8_t p256_scalar_valid_be32(const uint8_t src[32]);
void p256_scalar_add(p256_fe_t *out, const p256_fe_t *a, const p256_fe_t *b);
void p256_scalar_mul(p256_fe_t *out, const p256_fe_t *a, const p256_fe_t *b);
void p256_scalar_inv(p256_fe_t *out, const p256_fe_t *a);
uint8_t p256_point_from_be32(p256_point_t *out,
                             const uint8_t x[32],
                             const uint8_t y[32]);
uint8_t p256_point_is_on_curve(const p256_point_t *p);
void p256_base_point(p256_point_t *out);
void p256_point_add(p256_point_t *out, const p256_point_t *a, const p256_point_t *b);
void p256_point_double(p256_point_t *out, const p256_point_t *p);
void p256_point_mul_u32(p256_point_t *out, const p256_point_t *p, uint32_t scalar);
void p256_point_mul_be32(p256_point_t *out, const p256_point_t *p, const uint8_t scalar[32]);
void p256_point_mul_be32_projective(p256_point_t *out,
                                    const p256_point_t *p,
                                    const uint8_t scalar[32]);
uint32_t p256_selftest(void);

#endif
