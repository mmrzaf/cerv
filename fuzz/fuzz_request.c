#include "base/bounds.h"
#include "http/http_request.h"
#include "http/http_target.h"
#include "fuzz_support.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    struct cerv_http_request req;
    enum cerv_parse_result result = cerv_http_request_parse(data, size, &req);

    if (result == CERV_PARSE_OK) {
        struct cerv_path path;
        cerv_fuzz_require(size <= CERV_REQUEST_BYTES_MAX);
        cerv_fuzz_require(req.header_bytes == size);
        cerv_fuzz_require(req.field_count <= CERV_FIELD_COUNT_MAX);
        cerv_fuzz_require(req.version_major == 1U);
        cerv_fuzz_require(req.method == CERV_METHOD_GET || req.method == CERV_METHOD_HEAD);
        cerv_fuzz_require(req.target.form == CERV_TARGET_ORIGIN || req.target.form == CERV_TARGET_ABSOLUTE);
        cerv_fuzz_require(cerv_fuzz_span_within(data, size, req.method_raw));
        cerv_fuzz_require(cerv_fuzz_span_within(data, size, req.target.raw));
        if (req.host.ptr != NULL) cerv_fuzz_require(cerv_fuzz_span_within(data, size, req.host));
        if (req.effective_authority.ptr != NULL) cerv_fuzz_require(cerv_fuzz_span_within(data, size, req.effective_authority));
        for (size_t i = 0U; i < req.if_none_match_count; ++i) {
            cerv_fuzz_require(cerv_fuzz_span_within(data, size, req.if_none_match[i]));
        }
        if (cerv_http_target_decode_path(&req.target, &path) == CERV_TARGET_OK) {
            cerv_fuzz_require(path.len < CERV_PATH_BYTES_MAX);
            cerv_fuzz_require(path.bytes[path.len] == 0U);
            cerv_fuzz_require(memchr(path.bytes, 0, path.len) == NULL);
        }
    }
    return 0;
}
