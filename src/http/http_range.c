#include "http/http_range.h"

#include "base/checked.h"
#include "base/bounds.h"
#include "http/http_fields.h"

#include <stdbool.h>

static bool cerv_parse_one(struct cerv_span item, struct cerv_range_spec *out)
{
    size_t dash = 0U;
    uint64_t left = UINT64_C(0), right = UINT64_C(0);
    if (out == NULL || item.ptr == NULL || item.len == 0U) return false;
    while (dash < item.len && item.ptr[dash] != (unsigned char)'-') {
        if (item.ptr[dash] == (unsigned char)' ' || item.ptr[dash] == (unsigned char)'\t') return false;
        ++dash;
    }
    if (dash >= item.len) return false;
    for (size_t i = dash + 1U; i < item.len; ++i) {
        if (item.ptr[i] == (unsigned char)'-' || item.ptr[i] == (unsigned char)' ' || item.ptr[i] == (unsigned char)'\t') return false;
    }
    if (dash == 0U) {
        if (item.len == 1U || !cerv_u64_decimal(item.ptr + 1U, item.len - 1U, &right)) return false;
        *out = (struct cerv_range_spec){.kind = CERV_RANGE_SUFFIX, .first = right, .second = 0U};
        return true;
    }
    if (!cerv_u64_decimal(item.ptr, dash, &left)) return false;
    if (dash + 1U == item.len) {
        *out = (struct cerv_range_spec){.kind = CERV_RANGE_START_OPEN, .first = left, .second = 0U};
        return true;
    }
    if (!cerv_u64_decimal(item.ptr + dash + 1U, item.len - dash - 1U, &right) || right < left) return false;
    *out = (struct cerv_range_spec){.kind = CERV_RANGE_START_END, .first = left, .second = right};
    return true;
}


static bool cerv_other_range_set_valid(struct cerv_span value, size_t pos)
{
    unsigned count = 0U;
    const size_t first_pos = pos;

    for (;;) {
        size_t end = pos;
        struct cerv_span raw_item;
        struct cerv_span item;

        while (end < value.len && value.ptr[end] != (unsigned char)',') ++end;
        raw_item = (struct cerv_span){.ptr = value.ptr + pos, .len = end - pos};
        item = cerv_span_trim_ows(raw_item);
        if (item.len != 0U) {
            if (pos == first_pos && raw_item.len != 0U &&
                (raw_item.ptr[0] == (unsigned char)' ' || raw_item.ptr[0] == (unsigned char)'\t')) {
                return false;
            }
            for (size_t i = 0U; i < item.len; ++i) {
                unsigned char c = item.ptr[i];
                if (c < 0x21U || c > 0x7eU || c == (unsigned char)',') return false;
            }
            ++count;
        }
        if (end == value.len) break;
        pos = end + 1U;
    }
    return count != 0U;
}

enum cerv_range_parse_result cerv_http_range_parse(struct cerv_span value, struct cerv_range_spec *out)
{
    size_t eq = 0U;
    size_t pos = 0U;
    unsigned count = 0U;
    struct cerv_range_spec first = {0};
    struct cerv_span unit;
    if ((value.ptr == NULL && value.len != 0U) || value.len > CERV_FIELD_LINE_MAX) return CERV_RANGE_MALFORMED;
    value = cerv_span_trim_ows(value);
    if (out == NULL || value.ptr == NULL || value.len == 0U) return CERV_RANGE_MALFORMED;
    while (eq < value.len && value.ptr[eq] != (unsigned char)'=') ++eq;
    if (eq == 0U || eq + 1U > value.len) return CERV_RANGE_MALFORMED;
    unit = (struct cerv_span){.ptr = value.ptr, .len = eq};
    if (!cerv_http_is_token(unit)) return CERV_RANGE_MALFORMED;
    pos = eq + 1U;
    if (!cerv_span_equal_ascii_ci(unit, "bytes")) {
        return cerv_other_range_set_valid(value, pos) ? CERV_RANGE_UNIT_UNSUPPORTED : CERV_RANGE_MALFORMED;
    }
    for (;;) {
        size_t end = pos;
        struct cerv_span raw_item;
        struct cerv_span item;
        struct cerv_range_spec parsed;
        while (end < value.len && value.ptr[end] != (unsigned char)',') ++end;
        raw_item = (struct cerv_span){.ptr = value.ptr + pos, .len = end - pos};
        item = cerv_span_trim_ows(raw_item);
        if (item.len != 0U) {
            /* OWS belongs around list separators, not before the first non-empty range-spec. */
            if (pos == eq + 1U && raw_item.len != 0U &&
                (raw_item.ptr[0] == (unsigned char)' ' || raw_item.ptr[0] == (unsigned char)'\t')) {
                return CERV_RANGE_MALFORMED;
            }
            if (!cerv_parse_one(item, &parsed)) return CERV_RANGE_MALFORMED;
            if (count == 0U) first = parsed;
            ++count;
        }
        if (end == value.len) break;
        pos = end + 1U;
    }
    if (count == 0U) return CERV_RANGE_MALFORMED;
    *out = first;
    return count == 1U ? CERV_RANGE_SINGLE : CERV_RANGE_MULTIPLE;
}

enum cerv_range_normalize_result cerv_http_range_normalize(struct cerv_range_spec spec, uint64_t length, struct cerv_range_selection *out)
{
    struct cerv_range_selection selected = {0};
    if (out == NULL || length == UINT64_C(0)) return CERV_RANGE_UNSATISFIABLE;
    switch (spec.kind) {
    case CERV_RANGE_START_END:
        if (spec.second < spec.first || spec.first >= length) return CERV_RANGE_UNSATISFIABLE;
        selected.start = spec.first;
        selected.end = spec.second >= length ? length - UINT64_C(1) : spec.second;
        break;
    case CERV_RANGE_START_OPEN:
        if (spec.first >= length) return CERV_RANGE_UNSATISFIABLE;
        selected.start = spec.first;
        selected.end = length - UINT64_C(1);
        break;
    case CERV_RANGE_SUFFIX:
        if (spec.first == UINT64_C(0)) return CERV_RANGE_UNSATISFIABLE;
        selected.end = length - UINT64_C(1);
        selected.start = spec.first >= length ? UINT64_C(0) : length - spec.first;
        break;
    }
    selected.count = selected.end - selected.start + UINT64_C(1);
    *out = selected;
    return CERV_RANGE_SATISFIABLE;
}
