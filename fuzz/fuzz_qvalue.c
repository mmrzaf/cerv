#include "http/http_fields.h"
#include "fuzz_support.h"

#include <stddef.h>
#include <stdint.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    uint16_t q = 0U;
    if (cerv_http_qvalue_parse((struct cerv_span){.ptr = data, .len = size}, &q) == CERV_Q_OK) {
        cerv_fuzz_require(q <= 1000U);
        cerv_fuzz_require(size >= 1U && size <= 5U);
    }
    return 0;
}
