#ifndef CERV_HTTP_DATE_H
#define CERV_HTTP_DATE_H

#include "base/slice.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct cerv_http_date {
    int64_t unix_seconds;
    int year;
    unsigned month;
    unsigned day;
    unsigned hour;
    unsigned minute;
    unsigned second;
};

bool cerv_http_date_parse(struct cerv_span in, int current_year, struct cerv_http_date *out);
bool cerv_http_date_format_imf(int64_t unix_seconds, unsigned char out[29]);

#endif
