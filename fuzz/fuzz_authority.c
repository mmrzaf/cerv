#include "http/http_fields.h"
#include "fuzz_support.h"

#include <stddef.h>
#include <stdint.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    struct cerv_authority authority;
    bool allow_empty = size != 0U && (data[0] & 1U) != 0U;
    struct cerv_span input = {.ptr = data, .len = size};

    if (cerv_http_authority_parse(input, allow_empty, &authority)) {
        cerv_fuzz_require(cerv_fuzz_span_within(data, size, authority.host));
        if (authority.has_port) cerv_fuzz_require(cerv_fuzz_span_within(data, size, authority.port));
        if (!allow_empty) cerv_fuzz_require(authority.host.len != 0U);
    }
    return 0;
}
