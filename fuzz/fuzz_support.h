#ifndef CERV_FUZZ_SUPPORT_H
#define CERV_FUZZ_SUPPORT_H

#include "base/slice.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

static inline void cerv_fuzz_require(int condition)
{
    if (!condition) abort();
}

static inline int cerv_fuzz_span_within(const uint8_t *data, size_t size, struct cerv_span span)
{
    uintptr_t base = (uintptr_t)data;
    uintptr_t end = base + size;
    uintptr_t p = (uintptr_t)span.ptr;

    if (span.len == 0U && span.ptr == NULL) return 1;
    if (p < base || p > end) return 0;
    if (span.len > size) return 0;
    return p <= end - span.len;
}

#endif
