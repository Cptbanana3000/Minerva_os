#ifndef MINERVA_SHA256_H
#define MINERVA_SHA256_H

#include <stdint.h>

void sha256_hash(const uint8_t *data, uint32_t len, uint8_t out[32]);

#endif
