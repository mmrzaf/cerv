#include "base/path.h"

#include <string.h>

bool cerv_path_is_hidden(const unsigned char *bytes, size_t len)
{
    size_t segment_start = 0U;
    size_t i;
    if (bytes == NULL) return false;
    for (i = 0U; i <= len; ++i) {
        if (i == len || bytes[i] == (unsigned char)'/') {
            size_t segment_len = i - segment_start;
            if (segment_len != 0U && bytes[segment_start] == (unsigned char)'.') {
                bool well_known = segment_start == 0U && segment_len == strlen(CERV_WELL_KNOWN_SEGMENT) &&
                                  memcmp(bytes, CERV_WELL_KNOWN_SEGMENT, segment_len) == 0;
                if (!well_known) return true;
            }
            segment_start = i + 1U;
        }
    }
    return false;
}

bool cerv_path_last_segment_has_dot(const unsigned char *bytes, size_t len)
{
    size_t i;
    if (bytes == NULL) return false;
    for (i = len; i > 0U; --i) {
        if (bytes[i - 1U] == (unsigned char)'/') return false;
        if (bytes[i - 1U] == (unsigned char)'.') return true;
    }
    return false;
}
