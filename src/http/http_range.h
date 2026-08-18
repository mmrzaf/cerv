#ifndef CERV_HTTP_RANGE_H
#define CERV_HTTP_RANGE_H

#include "base/slice.h"

#include <stdint.h>

enum cerv_range_parse_result {
    CERV_RANGE_SINGLE = 0,
    CERV_RANGE_MULTIPLE,
    CERV_RANGE_UNIT_UNSUPPORTED,
    CERV_RANGE_MALFORMED
};

enum cerv_range_kind {
    CERV_RANGE_START_END = 0,
    CERV_RANGE_START_OPEN,
    CERV_RANGE_SUFFIX
};

struct cerv_range_spec {
    enum cerv_range_kind kind;
    uint64_t first;
    uint64_t second;
};

enum cerv_range_normalize_result {
    CERV_RANGE_SATISFIABLE = 0,
    CERV_RANGE_UNSATISFIABLE
};

struct cerv_range_selection {
    uint64_t start;
    uint64_t end;
    uint64_t count;
};

enum cerv_range_parse_result cerv_http_range_parse(struct cerv_span value, struct cerv_range_spec *out);
enum cerv_range_normalize_result cerv_http_range_normalize(struct cerv_range_spec spec, uint64_t length, struct cerv_range_selection *out);

#endif
