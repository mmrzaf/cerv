#include "serve/response.h"

#include "base/buffer.h"
#include "base/checked.h"
#include "http/http_date.h"
#include "http/http_fields.h"
#include "http/http_range.h"
#include "base/invariant.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

struct cerv_error_def {
    enum cerv_http_status status;
    const char *reason;
    const unsigned char *body;
    size_t body_len;
};

#define CERV_ERROR_DEF(code, phrase) \
    { CERV_STATUS_##code, #code " " phrase, (const unsigned char *)#code " " phrase "\n", sizeof(#code " " phrase "\n") - 1U }

static const struct cerv_error_def cerv_errors[] = {
    CERV_ERROR_DEF(400, "Bad Request"),
    CERV_ERROR_DEF(403, "Forbidden"),
    CERV_ERROR_DEF(404, "Not Found"),
    CERV_ERROR_DEF(405, "Method Not Allowed"),
    CERV_ERROR_DEF(406, "Not Acceptable"),
    CERV_ERROR_DEF(408, "Request Timeout"),
    CERV_ERROR_DEF(414, "URI Too Long"),
    CERV_ERROR_DEF(416, "Range Not Satisfiable"),
    CERV_ERROR_DEF(417, "Expectation Failed"),
    CERV_ERROR_DEF(431, "Request Header Fields Too Large"),
    CERV_ERROR_DEF(500, "Internal Server Error"),
    CERV_ERROR_DEF(501, "Not Implemented"),
    CERV_ERROR_DEF(503, "Service Unavailable"),
    CERV_ERROR_DEF(505, "HTTP Version Not Supported")
};

static bool cerv_append_literal(struct cerv_buffer *buf, const char *literal)
{
    return cerv_buffer_append(buf, literal, strlen(literal));
}

static const struct cerv_error_def *cerv_error_lookup(enum cerv_http_status status)
{
    size_t i;
    for (i = 0U; i < sizeof(cerv_errors) / sizeof(cerv_errors[0]); ++i) {
        if (cerv_errors[i].status == status) return &cerv_errors[i];
    }
    return NULL;
}

static const char *cerv_success_reason(enum cerv_http_status status)
{
    switch (status) {
    case CERV_STATUS_200: return "200 OK";
    case CERV_STATUS_206: return "206 Partial Content";
    case CERV_STATUS_304: return "304 Not Modified";
    case CERV_STATUS_400:
    case CERV_STATUS_403:
    case CERV_STATUS_404:
    case CERV_STATUS_405:
    case CERV_STATUS_406:
    case CERV_STATUS_408:
    case CERV_STATUS_414:
    case CERV_STATUS_416:
    case CERV_STATUS_417:
    case CERV_STATUS_431:
    case CERV_STATUS_500:
    case CERV_STATUS_501:
    case CERV_STATUS_503:
    case CERV_STATUS_505:
        break;
    }
    return NULL;
}

static bool cerv_append_status_line(struct cerv_buffer *buf, const char *status_text)
{
    return cerv_append_literal(buf, "HTTP/1.1 ") && cerv_append_literal(buf, status_text) &&
           cerv_append_literal(buf, "\r\n");
}

static bool cerv_append_date_connection(struct cerv_buffer *buf, const unsigned char date[29],
                                        bool close_connection)
{
    if (!cerv_append_literal(buf, "Date: ") || !cerv_buffer_append(buf, date, 29U) ||
        !cerv_append_literal(buf, "\r\n")) return false;
    return !close_connection || cerv_append_literal(buf, "Connection: close\r\n");
}

static bool cerv_append_u64_field(struct cerv_buffer *buf, const char *name, uint64_t value)
{
    return cerv_append_literal(buf, name) && cerv_buffer_append_u64(buf, value) && cerv_append_literal(buf, "\r\n");
}

static bool cerv_make_date(int64_t unix_seconds, unsigned char date[29], int *year)
{
    unsigned y = 0U;
    if (!cerv_http_date_format_imf(unix_seconds, date)) return false;
    y = (unsigned)(date[12] - (unsigned char)'0') * 1000U +
        (unsigned)(date[13] - (unsigned char)'0') * 100U +
        (unsigned)(date[14] - (unsigned char)'0') * 10U +
        (unsigned)(date[15] - (unsigned char)'0');
    *year = (int)y;
    return true;
}

static bool cerv_effective_last_modified(const struct cerv_representation *rep, int64_t now,
                                         int64_t *last_modified, unsigned char wire[29])
{
    int64_t value;
    if (rep == NULL || last_modified == NULL) return false;
    value = rep->file.mtime_sec > now ? now : rep->file.mtime_sec;
    if (!cerv_http_date_format_imf(value, wire)) return false;
    *last_modified = value;
    return true;
}

static bool cerv_if_none_match_304(const struct cerv_http_request *request,
                                   const struct cerv_representation *representation)
{
    struct cerv_entity_tag current;
    struct cerv_span wire = cerv_representation_etag(representation);
    if (request->if_none_match_count == 0U) return false;
    if (cerv_entity_tag_parse(wire, &current) != CERV_ETAG_OK) return false;
    return cerv_http_request_if_none_match_matches(request, current);
}

static bool cerv_if_modified_since_304(const struct cerv_http_request *request, int current_year,
                                       bool has_last_modified, int64_t last_modified)
{
    struct cerv_http_date condition;
    if (request->if_none_match_count != 0U || request->if_modified_since_count != 1U || !has_last_modified) {
        return false;
    }
    if (!cerv_http_date_parse(request->if_modified_since, current_year, &condition)) return false;
    return last_modified <= condition.unix_seconds;
}

/*
 * If-Range: a range is honored only when the validator is a strong entity-tag that equals the selected
 * representation's ETag. HTTP-date validators are never accepted: a descriptor mtime cannot prove the bytes did
 * not change twice within one HTTP-date second, so the full representation is sent instead.
 */
static bool cerv_if_range_allows_range(const struct cerv_http_request *request, int current_year,
                                       const struct cerv_representation *representation)
{
    struct cerv_if_range_value condition;
    struct cerv_entity_tag current;
    struct cerv_span current_wire;

    if (request->if_range_count == 0U) return true;
    if (request->if_range_count != 1U) return false;
    if (cerv_http_if_range_parse(request->if_range, current_year, &condition) != CERV_IF_RANGE_ETAG) return false;
    current_wire = cerv_representation_etag(representation);
    if (cerv_entity_tag_parse(current_wire, &current) != CERV_ETAG_OK) return false;
    return cerv_entity_tag_strong_equal(condition.etag, current);
}

static bool cerv_append_cache_control(struct cerv_buffer *buf, bool immutable)
{
    return cerv_append_literal(buf, immutable ?
        "Cache-Control: public, max-age=31536000, immutable\r\n" :
        "Cache-Control: no-cache\r\n");
}

static bool cerv_append_representation_common(struct cerv_buffer *buf, const struct cerv_representation *rep,
                                              const unsigned char last_modified[29], bool has_last_modified,
                                              bool immutable)
{
    const char *encoding = cerv_content_encoding_name(rep->encoding);
    if (!cerv_append_literal(buf, "Vary: Accept-Encoding\r\nETag: ") ||
        !cerv_buffer_append(buf, rep->etag, rep->etag_len) || !cerv_append_literal(buf, "\r\n")) return false;
    if (has_last_modified && (!cerv_append_literal(buf, "Last-Modified: ") ||
        !cerv_buffer_append(buf, last_modified, 29U) || !cerv_append_literal(buf, "\r\n"))) return false;
    if (!cerv_append_cache_control(buf, immutable)) return false;
    if (encoding != NULL && (!cerv_append_literal(buf, "Content-Encoding: ") ||
        !cerv_append_literal(buf, encoding) || !cerv_append_literal(buf, "\r\n"))) return false;
    return true;
}

static bool cerv_plan_304(const struct cerv_representation *rep, const unsigned char date[29],
                          const unsigned char last_modified[29], bool has_last_modified, bool immutable,
                          bool close_connection, struct cerv_response_plan *out)
{
    struct cerv_buffer buf;
    cerv_buffer_init(&buf, out->headers, sizeof(out->headers));
    if (!cerv_append_status_line(&buf, "304 Not Modified") || !cerv_append_date_connection(&buf, date, close_connection) ||
        !cerv_append_representation_common(&buf, rep, last_modified, has_last_modified, immutable) ||
        !cerv_append_literal(&buf, "\r\n")) return false;
    out->status = CERV_STATUS_304;
    out->header_len = buf.used;
    out->send_body = false;
    out->send_file = false;
    return true;
}

static bool cerv_plan_file(enum cerv_http_status status, const struct cerv_representation *rep,
                           const unsigned char date[29], const unsigned char last_modified[29],
                           bool has_last_modified, bool immutable, uint64_t offset, uint64_t count,
                           bool head_request, bool close_connection, struct cerv_response_plan *out)
{
    struct cerv_buffer buf;
    const char *status_text = cerv_success_reason(status);
    const char *encoding = cerv_content_encoding_name(rep->encoding);
    uint64_t range_end = UINT64_C(0);

    if (status == CERV_STATUS_200) {
        if (offset != UINT64_C(0) || count != rep->file.size) return false;
    } else if (status == CERV_STATUS_206) {
        uint64_t delta;
        if (count == UINT64_C(0) || !cerv_u64_sub(count, UINT64_C(1), &delta) ||
            !cerv_u64_add(offset, delta, &range_end) || range_end >= rep->file.size) return false;
    } else {
        return false;
    }

    cerv_buffer_init(&buf, out->headers, sizeof(out->headers));
    if (status_text == NULL || !cerv_append_status_line(&buf, status_text) || !cerv_append_date_connection(&buf, date, close_connection) ||
        !cerv_append_literal(&buf, "Content-Type: ") || !cerv_append_literal(&buf, rep->media_type) ||
        !cerv_append_literal(&buf, "\r\n")) return false;
    if (encoding != NULL && (!cerv_append_literal(&buf, "Content-Encoding: ") ||
        !cerv_append_literal(&buf, encoding) || !cerv_append_literal(&buf, "\r\n"))) return false;
    if (!cerv_append_literal(&buf, "Vary: Accept-Encoding\r\nETag: ") ||
        !cerv_buffer_append(&buf, rep->etag, rep->etag_len) || !cerv_append_literal(&buf, "\r\n")) return false;
    if (has_last_modified && (!cerv_append_literal(&buf, "Last-Modified: ") ||
        !cerv_buffer_append(&buf, last_modified, 29U) || !cerv_append_literal(&buf, "\r\n"))) return false;
    if (!cerv_append_cache_control(&buf, immutable) || !cerv_append_literal(&buf, "Accept-Ranges: bytes\r\n")) return false;
    if (status == CERV_STATUS_206) {
        if (!cerv_append_literal(&buf, "Content-Range: bytes ") || !cerv_buffer_append_u64(&buf, offset) ||
            !cerv_append_literal(&buf, "-") || !cerv_buffer_append_u64(&buf, range_end) ||
            !cerv_append_literal(&buf, "/") || !cerv_buffer_append_u64(&buf, rep->file.size) ||
            !cerv_append_literal(&buf, "\r\n")) return false;
    }
    if (!cerv_append_u64_field(&buf, "Content-Length: ", count) ||
        !cerv_append_literal(&buf, "X-Content-Type-Options: nosniff\r\n\r\n")) return false;
    out->status = status;
    out->header_len = buf.used;
    out->body = NULL;
    out->body_len = 0U;
    out->send_body = false;
    out->send_file = !head_request && count != UINT64_C(0);
    if (!cerv_u64_to_off_t(offset, &out->file_offset)) return false;
    out->file_count = count;
    return true;
}

static bool cerv_plan_416(const struct cerv_representation *rep, const unsigned char date[29],
                          bool head_request, bool close_connection, struct cerv_response_plan *out)
{
    const struct cerv_error_def *def = cerv_error_lookup(CERV_STATUS_416);
    struct cerv_buffer buf;
    if (def == NULL) return false;
    cerv_buffer_init(&buf, out->headers, sizeof(out->headers));
    if (!cerv_append_status_line(&buf, def->reason) || !cerv_append_date_connection(&buf, date, close_connection) ||
        !cerv_append_literal(&buf, "Content-Range: bytes */") || !cerv_buffer_append_u64(&buf, rep->file.size) ||
        !cerv_append_literal(&buf, "\r\nContent-Type: text/plain; charset=utf-8\r\n") ||
        !cerv_append_u64_field(&buf, "Content-Length: ", (uint64_t)def->body_len) ||
        !cerv_append_literal(&buf, "X-Content-Type-Options: nosniff\r\n\r\n")) return false;
    out->status = CERV_STATUS_416;
    out->header_len = buf.used;
    out->body = def->body;
    out->body_len = def->body_len;
    out->send_body = !head_request;
    out->send_file = false;
    return true;
}

bool cerv_response_plan_error(enum cerv_http_status status, bool head_request,
                              int64_t response_unix_seconds, struct cerv_response_plan *out)
{
    const struct cerv_error_def *def = cerv_error_lookup(status);
    unsigned char date[29];
    int year = 0;
    struct cerv_buffer buf;
    if (out == NULL || def == NULL || status == CERV_STATUS_416 ||
        !cerv_make_date(response_unix_seconds, date, &year)) return false;
    (void)year;
    *out = (struct cerv_response_plan){0};
    cerv_buffer_init(&buf, out->headers, sizeof(out->headers));
    if (!cerv_append_status_line(&buf, def->reason) || !cerv_append_date_connection(&buf, date, true)) return false;
    if (status == CERV_STATUS_405 && !cerv_append_literal(&buf, "Allow: GET, HEAD\r\n")) return false;
    if (status == CERV_STATUS_503 && !cerv_append_literal(&buf, "Retry-After: 1\r\nCache-Control: no-store\r\n")) return false;
    if (!cerv_append_literal(&buf, "Content-Type: text/plain; charset=utf-8\r\n") ||
        !cerv_append_u64_field(&buf, "Content-Length: ", (uint64_t)def->body_len) ||
        !cerv_append_literal(&buf, "X-Content-Type-Options: nosniff\r\n\r\n")) return false;
    out->status = status;
    out->header_len = buf.used;
    out->body = def->body;
    out->body_len = def->body_len;
    out->send_body = !head_request;
    out->send_file = false;
    return true;
}

enum cerv_http_status cerv_response_status_from_parse(enum cerv_parse_result result)
{
    switch (result) {
    case CERV_PARSE_BAD_REQUEST: return CERV_STATUS_400;
    case CERV_PARSE_URI_TOO_LONG: return CERV_STATUS_414;
    case CERV_PARSE_HEADERS_TOO_LARGE: return CERV_STATUS_431;
    case CERV_PARSE_METHOD_NOT_ALLOWED: return CERV_STATUS_405;
    case CERV_PARSE_NOT_IMPLEMENTED: return CERV_STATUS_501;
    case CERV_PARSE_HTTP_VERSION_UNSUPPORTED: return CERV_STATUS_505;
    case CERV_PARSE_EXPECTATION_FAILED: return CERV_STATUS_417;
    case CERV_PARSE_INCOMPLETE:
    case CERV_PARSE_OK:
        break;
    }
    return CERV_STATUS_500;
}

static enum cerv_http_status cerv_status_from_representation(enum cerv_representation_result result)
{
    switch (result) {
    case CERV_REPRESENTATION_NOT_FOUND: return CERV_STATUS_404;
    case CERV_REPRESENTATION_FORBIDDEN: return CERV_STATUS_403;
    case CERV_REPRESENTATION_NOT_ACCEPTABLE: return CERV_STATUS_406;
    case CERV_REPRESENTATION_FD_EXHAUSTED_PROCESS: return CERV_STATUS_500;
    case CERV_REPRESENTATION_FD_EXHAUSTED_SYSTEM: return CERV_STATUS_503;
    case CERV_REPRESENTATION_IO: return CERV_STATUS_500;
    case CERV_REPRESENTATION_UNSUPPORTED: return CERV_STATUS_500;
    case CERV_REPRESENTATION_OK:
        break;
    }
    return CERV_STATUS_500;
}

bool cerv_response_plan_resource_connection(const struct cerv_http_request *request,
                                            enum cerv_representation_result representation_result,
                                            const struct cerv_representation *representation,
                                            int64_t response_unix_seconds, bool immutable,
                                            bool close_connection, struct cerv_response_plan *out)
{
    unsigned char date[29];
    unsigned char last_modified_wire[29];
    int current_year = 0;
    int64_t last_modified = INT64_C(0);
    bool has_last_modified = false;
    bool head_request;

    if (request == NULL || out == NULL ||
        (request->method != CERV_METHOD_GET && request->method != CERV_METHOD_HEAD) ||
        !cerv_make_date(response_unix_seconds, date, &current_year)) return false;
    head_request = request->method == CERV_METHOD_HEAD;
    *out = (struct cerv_response_plan){0};
    if (representation_result == CERV_REPRESENTATION_UNSUPPORTED) {
        CERV_INVARIANT(false);
    }
    if (representation_result != CERV_REPRESENTATION_OK) {
        return cerv_response_plan_error(cerv_status_from_representation(representation_result), head_request,
                                        response_unix_seconds, out);
    }
    if (representation == NULL || representation->file.fd < 0 || representation->media_type == NULL ||
        representation->etag_len == 0U || representation->etag_len > CERV_ETAG_WIRE_MAX) return false;

    has_last_modified = cerv_effective_last_modified(representation, response_unix_seconds,
                                                     &last_modified, last_modified_wire);
    if (cerv_if_none_match_304(request, representation) ||
        cerv_if_modified_since_304(request, current_year, has_last_modified, last_modified)) {
        bool ok = cerv_plan_304(representation, date, last_modified_wire, has_last_modified, immutable,
                                close_connection, out);
        CERV_INVARIANT(ok);
        return true;
    }

    if (!head_request && request->range_field_count == 1U && request->range_result == CERV_RANGE_SINGLE &&
        cerv_if_range_allows_range(request, current_year, representation)) {
        struct cerv_range_selection selection;
        if (cerv_http_range_normalize(request->range, representation->file.size, &selection) == CERV_RANGE_UNSATISFIABLE) {
            bool ok = cerv_plan_416(representation, date, false, close_connection, out);
            CERV_INVARIANT(ok);
            return true;
        }
        {
            bool ok = cerv_plan_file(CERV_STATUS_206, representation, date, last_modified_wire,
                                     has_last_modified, immutable, selection.start, selection.count, false, close_connection, out);
            CERV_INVARIANT(ok);
            return true;
        }
    }
    {
        bool ok = cerv_plan_file(CERV_STATUS_200, representation, date, last_modified_wire,
                                 has_last_modified, immutable, UINT64_C(0), representation->file.size,
                                 head_request, close_connection, out);
        CERV_INVARIANT(ok);
        return true;
    }
}


bool cerv_response_plan_resource(const struct cerv_http_request *request,
                                 enum cerv_representation_result representation_result,
                                 const struct cerv_representation *representation,
                                 int64_t response_unix_seconds, bool immutable,
                                 struct cerv_response_plan *out)
{
    return cerv_response_plan_resource_connection(request, representation_result, representation,
                                                  response_unix_seconds, immutable, true, out);
}
