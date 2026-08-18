#ifndef CERV_HTTP_REQUEST_H
#define CERV_HTTP_REQUEST_H

#include "base/bounds.h"
#include "http/http_fields.h"
#include "http/http_range.h"
#include "http/http_target.h"
#include "base/slice.h"

#include <stdbool.h>
#include <stddef.h>

enum cerv_http_method {
    CERV_METHOD_GET = 0,
    CERV_METHOD_HEAD,
    CERV_METHOD_OTHER_KNOWN,
    CERV_METHOD_UNKNOWN
};

enum cerv_parse_result {
    CERV_PARSE_INCOMPLETE = 0,
    CERV_PARSE_OK,
    CERV_PARSE_BAD_REQUEST,
    CERV_PARSE_URI_TOO_LONG,
    CERV_PARSE_HEADERS_TOO_LARGE,
    CERV_PARSE_METHOD_NOT_ALLOWED,
    CERV_PARSE_NOT_IMPLEMENTED,
    CERV_PARSE_HTTP_VERSION_UNSUPPORTED,
    CERV_PARSE_EXPECTATION_FAILED
};

struct cerv_http_request {
    enum cerv_http_method method;
    unsigned version_major;
    unsigned version_minor;
    struct cerv_span method_raw;
    struct cerv_http_target target;
    struct cerv_span host;
    struct cerv_span effective_authority;
    struct cerv_accept_encoding accept_encoding;
    struct cerv_span if_none_match[CERV_FIELD_COUNT_MAX];
    size_t if_none_match_count;
    struct cerv_span if_modified_since;
    unsigned if_modified_since_count;
    struct cerv_range_spec range;
    enum cerv_range_parse_result range_result;
    unsigned range_field_count;
    struct cerv_span if_range;
    unsigned if_range_count;
    size_t field_count;
    size_t header_bytes;
    bool connection_close;
};

enum cerv_parse_result cerv_http_request_line_parse(struct cerv_span line, struct cerv_http_request *out);
enum cerv_parse_result cerv_http_request_parse(const unsigned char *buf, size_t received, struct cerv_http_request *out);
bool cerv_http_request_if_none_match_matches(const struct cerv_http_request *req, struct cerv_entity_tag current);

#endif
