#include "http/http_date.h"
#include "fuzz_support.h"

#include <stddef.h>
#include <stdint.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    struct cerv_http_date date;
    struct cerv_span input = {.ptr = data, .len = size};
    if (cerv_http_date_parse(input, 2026, &date)) {
        unsigned char formatted[29];
        struct cerv_http_date roundtrip;
        cerv_fuzz_require(date.year >= 0 && date.year <= 9999);
        cerv_fuzz_require(date.month >= 1U && date.month <= 12U);
        cerv_fuzz_require(date.day >= 1U && date.day <= 31U);
        cerv_fuzz_require(date.hour <= 23U && date.minute <= 59U && date.second <= 60U);
        if (cerv_http_date_format_imf(date.unix_seconds, formatted)) {
            cerv_fuzz_require(cerv_http_date_parse((struct cerv_span){.ptr = formatted, .len = sizeof(formatted)},
                                                   2026, &roundtrip));
            if (date.second != 60U) cerv_fuzz_require(roundtrip.unix_seconds == date.unix_seconds);
        }
    }
    return 0;
}
