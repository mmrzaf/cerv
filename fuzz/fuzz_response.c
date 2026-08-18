#include "fuzz_support.h"
#include "serve/response.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static uint64_t fuzz_u64(const uint8_t *data, size_t size, size_t offset)
{
    uint64_t value = UINT64_C(0);
    size_t available;
    size_t take;
    if (offset >= size) return UINT64_C(0);
    available = size - offset;
    take = available < sizeof(value) ? available : sizeof(value);
    memcpy(&value, data + offset, take);
    return value;
}

static void require_terminated(const struct cerv_response_plan *plan)
{
    cerv_fuzz_require(plan->header_len <= CERV_RESPONSE_BYTES_MAX);
    cerv_fuzz_require(plan->header_len >= 4U);
    cerv_fuzz_require(memcmp(plan->headers + plan->header_len - 4U, "\r\n\r\n", 4U) == 0);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    static const unsigned char etag[] = "W/\"fuzz\"";
    static const enum cerv_http_status errors[] = {
        CERV_STATUS_400, CERV_STATUS_403, CERV_STATUS_404, CERV_STATUS_405, CERV_STATUS_406,
        CERV_STATUS_408, CERV_STATUS_414, CERV_STATUS_417, CERV_STATUS_431,
        CERV_STATUS_500, CERV_STATUS_501, CERV_STATUS_503, CERV_STATUS_505
    };
    struct cerv_http_request request = {0};
    struct cerv_representation representation = {0};
    struct cerv_response_plan plan;
    int64_t now = INT64_C(784111777);
    uint64_t raw_size = fuzz_u64(data, size, 1U) & (uint64_t)INT64_MAX;
    bool close_connection;
    bool ok;

    if (size != 0U && (data[0] & 1U) != 0U) {
        enum cerv_http_status status = errors[(size_t)data[0] % (sizeof(errors) / sizeof(errors[0]))];
        ok = cerv_response_plan_error(status, (data[0] & 2U) != 0U, now, &plan);
        cerv_fuzz_require(ok);
        require_terminated(&plan);
        cerv_fuzz_require(!plan.send_file);
        return 0;
    }

    request.method = (size != 0U && (data[0] & 2U) != 0U) ? CERV_METHOD_HEAD : CERV_METHOD_GET;
    request.range_result = CERV_RANGE_UNIT_UNSUPPORTED;
    if (size != 0U && (data[0] & 4U) != 0U) {
        request.range_field_count = 1U;
        request.range_result = CERV_RANGE_SINGLE;
        request.range.kind = (enum cerv_range_kind)((size != 0U ? data[0] : 0U) % 3U);
        request.range.first = fuzz_u64(data, size, 9U);
        request.range.second = fuzz_u64(data, size, 17U);
    }
    representation.file.fd = 3;
    representation.file.size = raw_size;
    representation.file.mtime_sec = now - (int64_t)(fuzz_u64(data, size, 25U) % UINT64_C(100000));
    representation.encoding = (enum cerv_content_encoding)((size != 0U ? data[0] : 0U) % 3U);
    representation.media_type = "application/octet-stream";
    memcpy(representation.etag, etag, sizeof(etag) - 1U);
    representation.etag_len = sizeof(etag) - 1U;

    close_connection = size != 0U && (data[0] & 16U) != 0U;
    ok = cerv_response_plan_resource_connection(&request, CERV_REPRESENTATION_OK, &representation, now,
                                                size != 0U && (data[0] & 8U) != 0U,
                                                close_connection, &plan);
    cerv_fuzz_require(ok);
    require_terminated(&plan);
    cerv_fuzz_require(plan.status == CERV_STATUS_200 || plan.status == CERV_STATUS_206 ||
                      plan.status == CERV_STATUS_416);
    if (plan.status == CERV_STATUS_206) {
        cerv_fuzz_require(plan.file_count != UINT64_C(0));
        cerv_fuzz_require((uint64_t)plan.file_offset < representation.file.size);
        cerv_fuzz_require(plan.file_count <= representation.file.size - (uint64_t)plan.file_offset);
    }
    if (request.method == CERV_METHOD_HEAD) cerv_fuzz_require(!plan.send_file && !plan.send_body);
    return 0;
}
