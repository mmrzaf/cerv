#ifndef CERV_CHECKED_H
#define CERV_CHECKED_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

bool cerv_size_add(size_t a, size_t b, size_t *out);
bool cerv_size_sub(size_t a, size_t b, size_t *out);
bool cerv_size_mul(size_t a, size_t b, size_t *out);
bool cerv_u64_add(uint64_t a, uint64_t b, uint64_t *out);
bool cerv_u64_sub(uint64_t a, uint64_t b, uint64_t *out);
bool cerv_u64_mul(uint64_t a, uint64_t b, uint64_t *out);
bool cerv_u64_decimal(const unsigned char *p, size_t len, uint64_t *out);
bool cerv_u64_to_off_t(uint64_t value, off_t *out);

#endif
