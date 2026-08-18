#ifndef CERV_HTTP_TARGET_H
#define CERV_HTTP_TARGET_H

#include "base/bounds.h"
#include "base/slice.h"

#include <stdbool.h>
#include <stddef.h>

enum cerv_target_form {
    CERV_TARGET_ORIGIN = 0,
    CERV_TARGET_ABSOLUTE,
    CERV_TARGET_AUTHORITY,
    CERV_TARGET_ASTERISK
};

enum cerv_target_result {
    CERV_TARGET_OK = 0,
    CERV_TARGET_BAD_REQUEST,
    CERV_TARGET_TOO_LONG
};

struct cerv_http_target {
    enum cerv_target_form form;
    struct cerv_span raw;
    struct cerv_span authority;
    struct cerv_span path;
    struct cerv_span query;
    bool has_query;
    bool path_is_implicit_root;
};

struct cerv_path {
    unsigned char bytes[CERV_PATH_BYTES_MAX];
    size_t len;
};

enum cerv_target_result cerv_http_target_parse(struct cerv_span raw, struct cerv_http_target *out);
enum cerv_target_result cerv_http_target_decode_path(const struct cerv_http_target *target, struct cerv_path *out);

#endif
