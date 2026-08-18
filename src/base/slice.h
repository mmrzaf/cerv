#ifndef CERV_SLICE_H
#define CERV_SLICE_H

#include <stdbool.h>
#include <stddef.h>

struct cerv_span {
    const unsigned char *ptr;
    size_t len;
};

bool cerv_span_equal(struct cerv_span a, struct cerv_span b);
bool cerv_span_equal_ascii_ci(struct cerv_span a, const char *literal);
struct cerv_span cerv_span_trim_ows(struct cerv_span in);

#endif
