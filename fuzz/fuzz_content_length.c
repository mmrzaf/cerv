#include "http/http_fields.h"
#include "fuzz_support.h"

#include <stddef.h>
#include <stdint.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    uint64_t value = UINT64_C(0);
    struct cerv_span input = {.ptr = data, .len = size};
    if (cerv_http_content_length_parse(input, &value)) {
        struct cerv_span trimmed = cerv_span_trim_ows(input);
        cerv_fuzz_require(trimmed.len != 0U);
        for (size_t i = 0U; i < trimmed.len; ++i) {
            cerv_fuzz_require(trimmed.ptr[i] >= (unsigned char)'0' && trimmed.ptr[i] <= (unsigned char)'9');
        }
    }
    return 0;
}
