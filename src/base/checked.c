#include "base/checked.h"

#include <stdint.h>

_Static_assert(sizeof(off_t) == sizeof(int64_t), "Cerv requires 64-bit off_t");

bool cerv_size_add(size_t a, size_t b, size_t *out)
{
    if (out == NULL || b > SIZE_MAX - a) {
        return false;
    }
    *out = a + b;
    return true;
}

bool cerv_size_sub(size_t a, size_t b, size_t *out)
{
    if (out == NULL || b > a) {
        return false;
    }
    *out = a - b;
    return true;
}

bool cerv_size_mul(size_t a, size_t b, size_t *out)
{
    if (out == NULL || (a != 0U && b > SIZE_MAX / a)) {
        return false;
    }
    *out = a * b;
    return true;
}

bool cerv_u64_add(uint64_t a, uint64_t b, uint64_t *out)
{
    if (out == NULL || b > UINT64_MAX - a) {
        return false;
    }
    *out = a + b;
    return true;
}

bool cerv_u64_sub(uint64_t a, uint64_t b, uint64_t *out)
{
    if (out == NULL || b > a) {
        return false;
    }
    *out = a - b;
    return true;
}

bool cerv_u64_mul(uint64_t a, uint64_t b, uint64_t *out)
{
    if (out == NULL || (a != UINT64_C(0) && b > UINT64_MAX / a)) {
        return false;
    }
    *out = a * b;
    return true;
}

bool cerv_u64_decimal(const unsigned char *p, size_t len, uint64_t *out)
{
    uint64_t value = UINT64_C(0);
    size_t i = 0U;

    if (p == NULL || out == NULL || len == 0U || len > 20U) {
        return false;
    }
    for (i = 0U; i < len; ++i) {
        unsigned char c = p[i];
        uint64_t digit = UINT64_C(0);
        if (c < (unsigned char)'0' || c > (unsigned char)'9') {
            return false;
        }
        digit = (uint64_t)(c - (unsigned char)'0');
        if (value > (UINT64_MAX - digit) / UINT64_C(10)) {
            return false;
        }
        value = value * UINT64_C(10) + digit;
    }
    *out = value;
    return true;
}


bool cerv_u64_to_off_t(uint64_t value, off_t *out)
{
    if (out == NULL || value > (uint64_t)INT64_MAX) {
        return false;
    }
    *out = (off_t)value;
    return true;
}
