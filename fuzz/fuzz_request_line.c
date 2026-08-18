#include "base/bounds.h"
#include "http/http_request.h"
#include "fuzz_support.h"

#include <stddef.h>
#include <stdint.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    struct cerv_http_request req;
    enum cerv_parse_result result = cerv_http_request_line_parse(
        (struct cerv_span){.ptr = data, .len = size}, &req);

    if (result == CERV_PARSE_OK) {
        cerv_fuzz_require(size <= CERV_REQUEST_LINE_MAX);
        cerv_fuzz_require(req.version_major == 1U);
        cerv_fuzz_require(cerv_fuzz_span_within(data, size, req.method_raw));
        cerv_fuzz_require(cerv_fuzz_span_within(data, size, req.target.raw));
    }
    return 0;
}
