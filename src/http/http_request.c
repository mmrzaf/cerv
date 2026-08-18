#include "http/http_request.h"

#include "base/checked.h"

#include <stdint.h>
#include <string.h>

enum cerv_line_result {
    CERV_LINE_OK = 0,
    CERV_LINE_INCOMPLETE,
    CERV_LINE_BAD
};

static enum cerv_line_result cerv_next_line(const unsigned char *buf, size_t len, size_t start,
                                             size_t *line_end, size_t *next)
{
    size_t i = start;
    if (buf == NULL || line_end == NULL || next == NULL || start > len) return CERV_LINE_BAD;
    while (i < len) {
        if (buf[i] == (unsigned char)'\n') {
            if (i == start || buf[i - 1U] != (unsigned char)'\r') return CERV_LINE_BAD;
            *line_end = i - 1U;
            *next = i + 1U;
            return CERV_LINE_OK;
        }
        if (buf[i] == (unsigned char)'\r') {
            if (i + 1U >= len) return CERV_LINE_INCOMPLETE;
            if (buf[i + 1U] != (unsigned char)'\n') return CERV_LINE_BAD;
        }
        ++i;
    }
    return CERV_LINE_INCOMPLETE;
}

static bool cerv_version_parse(struct cerv_span in, unsigned *major, unsigned *minor)
{
    if (in.len != 8U || memcmp(in.ptr, "HTTP/", 5U) != 0 || in.ptr[6] != (unsigned char)'.' ||
        in.ptr[5] < (unsigned char)'0' || in.ptr[5] > (unsigned char)'9' ||
        in.ptr[7] < (unsigned char)'0' || in.ptr[7] > (unsigned char)'9') return false;
    *major = (unsigned)(in.ptr[5] - (unsigned char)'0');
    *minor = (unsigned)(in.ptr[7] - (unsigned char)'0');
    return true;
}

static enum cerv_http_method cerv_method_classify(struct cerv_span method)
{
    static const char *const known[] = {
        "POST", "PUT", "DELETE", "CONNECT", "OPTIONS", "TRACE", "PATCH"
    };
    size_t i = 0U;
    if (method.len == 3U && memcmp(method.ptr, "GET", 3U) == 0) return CERV_METHOD_GET;
    if (method.len == 4U && memcmp(method.ptr, "HEAD", 4U) == 0) return CERV_METHOD_HEAD;
    for (i = 0U; i < sizeof(known) / sizeof(known[0]); ++i) {
        size_t n = strlen(known[i]);
        if (method.len == n && memcmp(method.ptr, known[i], n) == 0) return CERV_METHOD_OTHER_KNOWN;
    }
    return CERV_METHOD_UNKNOWN;
}

static enum cerv_parse_result cerv_incomplete_request_line_limit_result(const unsigned char *buf, size_t len)
{
    size_t first_sp = 0U;
    size_t second_sp = 0U;
    struct cerv_span method;

    while (first_sp < len && buf[first_sp] != (unsigned char)' ') ++first_sp;
    if (first_sp == 0U || first_sp >= len) return CERV_PARSE_BAD_REQUEST;
    method = (struct cerv_span){.ptr = buf, .len = first_sp};
    if (!cerv_http_is_token(method)) return CERV_PARSE_BAD_REQUEST;
    second_sp = first_sp + 1U;
    while (second_sp < len && buf[second_sp] != (unsigned char)' ') ++second_sp;
    if (second_sp >= len) return CERV_PARSE_URI_TOO_LONG;
    if (second_sp + 9U > CERV_REQUEST_LINE_MAX) return CERV_PARSE_URI_TOO_LONG;
    return CERV_PARSE_BAD_REQUEST;
}

enum cerv_parse_result cerv_http_request_line_parse(struct cerv_span line, struct cerv_http_request *req)
{
    const unsigned char *buf = line.ptr;
    size_t line_len = line.len;
    size_t first_sp = 0U;
    size_t second_sp = 0U;
    struct cerv_span target_raw;
    enum cerv_target_result target_result;

    if (req == NULL || buf == NULL) return CERV_PARSE_BAD_REQUEST;
    *req = (struct cerv_http_request){0};
    cerv_accept_encoding_init(&req->accept_encoding);
    req->range_result = CERV_RANGE_UNIT_UNSUPPORTED;
    if (line_len > CERV_REQUEST_LINE_MAX) {
        return cerv_incomplete_request_line_limit_result(buf, CERV_REQUEST_LINE_MAX + 1U);
    }
    while (first_sp < line_len && buf[first_sp] != (unsigned char)' ') {
        if (buf[first_sp] == (unsigned char)'\t') return CERV_PARSE_BAD_REQUEST;
        ++first_sp;
    }
    if (first_sp == 0U || first_sp >= line_len) return CERV_PARSE_BAD_REQUEST;
    second_sp = first_sp + 1U;
    while (second_sp < line_len && buf[second_sp] != (unsigned char)' ') {
        if (buf[second_sp] == (unsigned char)'\t') return CERV_PARSE_BAD_REQUEST;
        ++second_sp;
    }
    if (second_sp <= first_sp + 1U || second_sp >= line_len) return CERV_PARSE_BAD_REQUEST;
    for (size_t i = second_sp + 1U; i < line_len; ++i) {
        if (buf[i] == (unsigned char)' ' || buf[i] == (unsigned char)'\t') return CERV_PARSE_BAD_REQUEST;
    }
    req->method_raw = (struct cerv_span){.ptr = buf, .len = first_sp};
    if (!cerv_http_is_token(req->method_raw)) return CERV_PARSE_BAD_REQUEST;
    req->method = cerv_method_classify(req->method_raw);
    target_raw = (struct cerv_span){.ptr = buf + first_sp + 1U, .len = second_sp - first_sp - 1U};
    target_result = cerv_http_target_parse(target_raw, &req->target);
    if (target_result == CERV_TARGET_TOO_LONG) return CERV_PARSE_URI_TOO_LONG;
    if (target_result != CERV_TARGET_OK) return CERV_PARSE_BAD_REQUEST;
    {
        struct cerv_span version = {.ptr = buf + second_sp + 1U, .len = line_len - second_sp - 1U};
        if (!cerv_version_parse(version, &req->version_major, &req->version_minor)) return CERV_PARSE_BAD_REQUEST;
    }
    if (req->version_major != 1U) return CERV_PARSE_HTTP_VERSION_UNSUPPORTED;
    if ((req->method == CERV_METHOD_GET || req->method == CERV_METHOD_HEAD) &&
        !(req->target.form == CERV_TARGET_ORIGIN || req->target.form == CERV_TARGET_ABSOLUTE)) {
        return CERV_PARSE_BAD_REQUEST;
    }
    return CERV_PARSE_OK;
}

static bool cerv_expect_supported(struct cerv_span value)
{
    size_t pos = 0U;
    value = cerv_span_trim_ows(value);
    for (;;) {
        size_t end = pos;
        struct cerv_span item;
        while (end < value.len && value.ptr[end] != (unsigned char)',') ++end;
        item = cerv_span_trim_ows((struct cerv_span){.ptr = value.ptr + pos, .len = end - pos});
        if (item.len != 0U && !cerv_span_equal_ascii_ci(item, "100-continue")) return false;
        if (end == value.len) break;
        pos = end + 1U;
    }
    return true;
}

static bool cerv_if_none_match_is_wildcard(struct cerv_span value)
{
    value = cerv_span_trim_ows(value);
    return value.len == 1U && value.ptr != NULL && value.ptr[0] == (unsigned char)'*';
}

static bool cerv_connection_add_field(struct cerv_span value, bool *connection_close)
{
    size_t pos = 0U;
    if (connection_close == NULL) return false;
    value = cerv_span_trim_ows(value);
    for (;;) {
        size_t end = pos;
        struct cerv_span item;
        while (end < value.len && value.ptr[end] != (unsigned char)',') ++end;
        item = cerv_span_trim_ows((struct cerv_span){.ptr = value.ptr + pos, .len = end - pos});
        if (item.len != 0U) {
            if (!cerv_http_is_token(item)) return false;
            if (cerv_span_equal_ascii_ci(item, "close")) *connection_close = true;
        }
        if (end == value.len) break;
        pos = end + 1U;
    }
    return true;
}

static enum cerv_parse_result cerv_process_field(struct cerv_span name, struct cerv_span value,
                                                  struct cerv_http_request *req, bool *host_seen,
                                                  bool *content_length_seen, bool *transfer_encoding_seen,
                                                  bool *unsupported_expectation)
{
    struct cerv_authority authority;
    uint64_t content_length = UINT64_C(0);
    if (cerv_span_equal_ascii_ci(name, "host")) {
        if (*host_seen) return CERV_PARSE_BAD_REQUEST;
        *host_seen = true;
        value = cerv_span_trim_ows(value);
        if (!cerv_http_authority_parse(value, true, &authority)) return CERV_PARSE_BAD_REQUEST;
        req->host = value;
        return CERV_PARSE_OK;
    }
    if (cerv_span_equal_ascii_ci(name, "transfer-encoding")) {
        *transfer_encoding_seen = true;
        return CERV_PARSE_OK;
    }
    if (cerv_span_equal_ascii_ci(name, "content-length")) {
        if (*content_length_seen) return CERV_PARSE_BAD_REQUEST;
        *content_length_seen = true;
        if (!cerv_http_content_length_parse(value, &content_length) || content_length != UINT64_C(0)) {
            return CERV_PARSE_BAD_REQUEST;
        }
        return CERV_PARSE_OK;
    }
    if (cerv_span_equal_ascii_ci(name, "accept-encoding")) {
        return cerv_accept_encoding_add_field(&req->accept_encoding, value) ? CERV_PARSE_OK : CERV_PARSE_BAD_REQUEST;
    }
    if (cerv_span_equal_ascii_ci(name, "if-none-match")) {
        bool wildcard = false;
        value = cerv_span_trim_ows(value);
        if (!cerv_if_none_match_valid(value) || req->if_none_match_count >= CERV_FIELD_COUNT_MAX) {
            return CERV_PARSE_BAD_REQUEST;
        }
        wildcard = cerv_if_none_match_is_wildcard(value);
        if ((wildcard && req->if_none_match_count != 0U) ||
            (!wildcard && req->if_none_match_count != 0U &&
             cerv_if_none_match_is_wildcard(req->if_none_match[0]))) {
            return CERV_PARSE_BAD_REQUEST;
        }
        req->if_none_match[req->if_none_match_count++] = value;
        return CERV_PARSE_OK;
    }
    if (cerv_span_equal_ascii_ci(name, "if-modified-since")) {
        ++req->if_modified_since_count;
        if (req->if_modified_since_count == 1U) req->if_modified_since = cerv_span_trim_ows(value);
        return CERV_PARSE_OK;
    }
    if (cerv_span_equal_ascii_ci(name, "range")) {
        struct cerv_range_spec parsed = {0};
        enum cerv_range_parse_result rr;
        if (req->range_field_count != 0U) return CERV_PARSE_BAD_REQUEST;
        req->range_field_count = 1U;
        rr = cerv_http_range_parse(value, &parsed);
        if (rr == CERV_RANGE_MALFORMED) return CERV_PARSE_BAD_REQUEST;
        req->range_result = rr;
        if (rr == CERV_RANGE_SINGLE) req->range = parsed;
        return CERV_PARSE_OK;
    }
    if (cerv_span_equal_ascii_ci(name, "if-range")) {
        ++req->if_range_count;
        if (req->if_range_count == 1U) req->if_range = cerv_span_trim_ows(value);
        return CERV_PARSE_OK;
    }
    if (cerv_span_equal_ascii_ci(name, "expect")) {
        if (!cerv_expect_supported(value)) *unsupported_expectation = true;
        return CERV_PARSE_OK;
    }
    if (cerv_span_equal_ascii_ci(name, "connection")) {
        return cerv_connection_add_field(value, &req->connection_close) ? CERV_PARSE_OK : CERV_PARSE_BAD_REQUEST;
    }
    return CERV_PARSE_OK;
}

enum cerv_parse_result cerv_http_request_parse(const unsigned char *buf, size_t received, struct cerv_http_request *out)
{
    struct cerv_http_request req = {0};
    size_t scan_len = received;
    size_t pos = 0U;
    size_t line_end = 0U;
    size_t next = 0U;
    bool host_seen = false;
    bool content_length_seen = false;
    bool transfer_encoding_seen = false;
    bool unsupported_expectation = false;
    enum cerv_line_result line_result;
    enum cerv_parse_result result;

    if (buf == NULL || out == NULL) return CERV_PARSE_BAD_REQUEST;
    cerv_accept_encoding_init(&req.accept_encoding);
    req.range_result = CERV_RANGE_UNIT_UNSUPPORTED;
    if (scan_len > CERV_REQUEST_BYTES_MAX + 1U) scan_len = CERV_REQUEST_BYTES_MAX + 1U;

    line_result = cerv_next_line(buf, scan_len, 0U, &line_end, &next);
    if (line_result == CERV_LINE_BAD) return CERV_PARSE_BAD_REQUEST;
    if (line_result == CERV_LINE_INCOMPLETE) {
        if (scan_len > CERV_REQUEST_LINE_MAX) {
            return cerv_incomplete_request_line_limit_result(buf, scan_len);
        }
        if (received > CERV_REQUEST_BYTES_MAX) return CERV_PARSE_HEADERS_TOO_LARGE;
        return CERV_PARSE_INCOMPLETE;
    }
    result = cerv_http_request_line_parse((struct cerv_span){.ptr = buf, .len = line_end}, &req);
    if (result != CERV_PARSE_OK) return result;
    pos = next;

    for (;;) {
        struct cerv_span line;
        size_t colon = 0U;
        struct cerv_span name;
        struct cerv_span value;

        if (pos > CERV_REQUEST_BYTES_MAX) return CERV_PARSE_HEADERS_TOO_LARGE;
        line_result = cerv_next_line(buf, scan_len, pos, &line_end, &next);
        if (line_result == CERV_LINE_BAD) return CERV_PARSE_BAD_REQUEST;
        if (line_result == CERV_LINE_INCOMPLETE) {
            if (scan_len - pos > CERV_FIELD_LINE_MAX) return CERV_PARSE_HEADERS_TOO_LARGE;
            if (received > CERV_REQUEST_BYTES_MAX) return CERV_PARSE_HEADERS_TOO_LARGE;
            return CERV_PARSE_INCOMPLETE;
        }
        if (next > CERV_REQUEST_BYTES_MAX) return CERV_PARSE_HEADERS_TOO_LARGE;
        line = (struct cerv_span){.ptr = buf + pos, .len = line_end - pos};
        if (line.len == 0U) {
            req.header_bytes = next;
            if (received != next) return CERV_PARSE_BAD_REQUEST;
            break;
        }
        if (line.len > CERV_FIELD_LINE_MAX) return CERV_PARSE_HEADERS_TOO_LARGE;
        if (line.ptr[0] == (unsigned char)' ' || line.ptr[0] == (unsigned char)'\t') return CERV_PARSE_BAD_REQUEST;
        ++req.field_count;
        if (req.field_count > CERV_FIELD_COUNT_MAX) return CERV_PARSE_HEADERS_TOO_LARGE;
        while (colon < line.len && line.ptr[colon] != (unsigned char)':') ++colon;
        if (colon == 0U || colon >= line.len) return CERV_PARSE_BAD_REQUEST;
        name = (struct cerv_span){.ptr = line.ptr, .len = colon};
        value = (struct cerv_span){.ptr = line.ptr + colon + 1U, .len = line.len - colon - 1U};
        if (!cerv_http_is_token(name) || !cerv_http_field_value_valid(value)) return CERV_PARSE_BAD_REQUEST;
        result = cerv_process_field(name, value, &req, &host_seen, &content_length_seen,
                                    &transfer_encoding_seen, &unsupported_expectation);
        if (result != CERV_PARSE_OK) return result;
        pos = next;
    }

    if (transfer_encoding_seen) return CERV_PARSE_BAD_REQUEST;
    if (req.version_minor == 0U) req.connection_close = true;
    if (req.version_minor >= 1U && !host_seen) return CERV_PARSE_BAD_REQUEST;
    if (unsupported_expectation) return CERV_PARSE_EXPECTATION_FAILED;
    if (req.target.form == CERV_TARGET_ABSOLUTE) req.effective_authority = req.target.authority;
    else if (host_seen) req.effective_authority = req.host;
    if (req.method == CERV_METHOD_OTHER_KNOWN) return CERV_PARSE_METHOD_NOT_ALLOWED;
    if (req.method == CERV_METHOD_UNKNOWN) return CERV_PARSE_NOT_IMPLEMENTED;
    *out = req;
    return CERV_PARSE_OK;
}

bool cerv_http_request_if_none_match_matches(const struct cerv_http_request *req, struct cerv_entity_tag current)
{
    size_t i = 0U;
    if (req == NULL) return false;
    for (i = 0U; i < req->if_none_match_count; ++i) {
        if (cerv_if_none_match_matches(req->if_none_match[i], current)) return true;
    }
    return false;
}
