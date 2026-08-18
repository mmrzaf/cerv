#include "http/http_fields.h"
#include "fuzz_support.h"

#include <stddef.h>
#include <stdint.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    struct cerv_if_range_value parsed;
    int current_year = 2026;
    enum cerv_if_range_parse_result result;

    if (size >= 2U) {
        unsigned year_bits = (unsigned)data[0] * 256U + (unsigned)data[1];
        current_year = (int)(year_bits % 10000U);
    }
    result = cerv_http_if_range_parse((struct cerv_span){.ptr = data, .len = size}, current_year, &parsed);
    switch (result) {
    case CERV_IF_RANGE_ETAG:
        cerv_fuzz_require(parsed.kind == CERV_IF_RANGE_ETAG);
        cerv_fuzz_require(cerv_fuzz_span_within(data, size, parsed.etag.opaque));
        break;
    case CERV_IF_RANGE_DATE:
        cerv_fuzz_require(parsed.kind == CERV_IF_RANGE_DATE);
        cerv_fuzz_require(parsed.date.year >= 0 && parsed.date.year <= 9999);
        cerv_fuzz_require(parsed.date.month >= 1U && parsed.date.month <= 12U);
        cerv_fuzz_require(parsed.date.day >= 1U && parsed.date.day <= 31U);
        cerv_fuzz_require(parsed.date.hour <= 23U && parsed.date.minute <= 59U && parsed.date.second <= 60U);
        break;
    case CERV_IF_RANGE_INVALID:
        break;
    }
    return 0;
}
