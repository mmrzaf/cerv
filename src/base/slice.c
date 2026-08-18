#include "base/slice.h"

#include <string.h>

static unsigned char cerv_ascii_lower(unsigned char c)
{
    if (c >= (unsigned char)'A' && c <= (unsigned char)'Z') {
        return (unsigned char)(c + ((unsigned char)'a' - (unsigned char)'A'));
    }
    return c;
}

bool cerv_span_equal(struct cerv_span a, struct cerv_span b)
{
    return a.len == b.len && (a.len == 0U || memcmp(a.ptr, b.ptr, a.len) == 0);
}

bool cerv_span_equal_ascii_ci(struct cerv_span a, const char *literal)
{
    size_t i = 0U;
    size_t literal_len = strlen(literal);

    if (a.len != literal_len) {
        return false;
    }
    for (i = 0U; i < a.len; ++i) {
        if (cerv_ascii_lower(a.ptr[i]) != cerv_ascii_lower((unsigned char)literal[i])) {
            return false;
        }
    }
    return true;
}

struct cerv_span cerv_span_trim_ows(struct cerv_span in)
{
    size_t first = 0U;
    size_t last = in.len;

    if (in.ptr == NULL) {
        return (struct cerv_span){.ptr = NULL, .len = 0U};
    }

    while (first < last && (in.ptr[first] == (unsigned char)' ' || in.ptr[first] == (unsigned char)'\t')) {
        ++first;
    }
    while (last > first && (in.ptr[last - 1U] == (unsigned char)' ' || in.ptr[last - 1U] == (unsigned char)'\t')) {
        --last;
    }
    return (struct cerv_span){.ptr = in.ptr + first, .len = last - first};
}
