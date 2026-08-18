#ifndef CERV_RESPONSE_H
#define CERV_RESPONSE_H

#include "base/bounds.h"
#include "http/http_request.h"
#include "serve/representation.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#define CERV_HTTP_DATE_WIRE_LEN ((size_t)29)

enum cerv_http_status {
    CERV_STATUS_200 = 200,
    CERV_STATUS_206 = 206,
    CERV_STATUS_304 = 304,
    CERV_STATUS_400 = 400,
    CERV_STATUS_403 = 403,
    CERV_STATUS_404 = 404,
    CERV_STATUS_405 = 405,
    CERV_STATUS_406 = 406,
    CERV_STATUS_408 = 408,
    CERV_STATUS_414 = 414,
    CERV_STATUS_416 = 416,
    CERV_STATUS_417 = 417,
    CERV_STATUS_431 = 431,
    CERV_STATUS_500 = 500,
    CERV_STATUS_501 = 501,
    CERV_STATUS_503 = 503,
    CERV_STATUS_505 = 505
};

struct cerv_response_plan {
    enum cerv_http_status status;
    unsigned char headers[CERV_RESPONSE_BYTES_MAX];
    size_t header_len;
    const unsigned char *body;
    size_t body_len;
    bool send_body;
    bool send_file;
    off_t file_offset;
    uint64_t file_count;
};

bool cerv_response_plan_resource(const struct cerv_http_request *request,
                                 enum cerv_representation_result representation_result,
                                 const struct cerv_representation *representation,
                                 int64_t response_unix_seconds, bool immutable,
                                 struct cerv_response_plan *out);
bool cerv_response_plan_resource_connection(const struct cerv_http_request *request,
                                            enum cerv_representation_result representation_result,
                                            const struct cerv_representation *representation,
                                            int64_t response_unix_seconds, bool immutable,
                                            bool close_connection, struct cerv_response_plan *out);
bool cerv_response_plan_error(enum cerv_http_status status, bool head_request,
                              int64_t response_unix_seconds, struct cerv_response_plan *out);
enum cerv_http_status cerv_response_status_from_parse(enum cerv_parse_result result);

#endif
