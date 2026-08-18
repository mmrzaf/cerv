#include "http/http_range.h"
#include "fuzz_support.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    struct cerv_range_spec spec;
    enum cerv_range_parse_result parsed = cerv_http_range_parse(
        (struct cerv_span){.ptr = data, .len = size}, &spec);
    if (parsed == CERV_RANGE_SINGLE) {
        uint64_t length = UINT64_C(0);
        struct cerv_range_selection selected;
        size_t take = size < sizeof(length) ? size : sizeof(length);
        if (take != 0U) memcpy(&length, data, take);
        if (cerv_http_range_normalize(spec, length, &selected) == CERV_RANGE_SATISFIABLE) {
            cerv_fuzz_require(length != UINT64_C(0));
            cerv_fuzz_require(selected.start <= selected.end);
            cerv_fuzz_require(selected.end < length);
            cerv_fuzz_require(selected.count == selected.end - selected.start + UINT64_C(1));
        }
    }
    return 0;
}
