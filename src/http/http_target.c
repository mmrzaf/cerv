#include "http/http_target.h"

#include "base/bounds.h"
#include "http/http_fields.h"

#include <string.h>

static bool cerv_is_alpha(unsigned char c)
{
    return (c >= (unsigned char)'A' && c <= (unsigned char)'Z') ||
           (c >= (unsigned char)'a' && c <= (unsigned char)'z');
}

static bool cerv_is_digit(unsigned char c)
{
    return c >= (unsigned char)'0' && c <= (unsigned char)'9';
}

static bool cerv_is_hexdig(unsigned char c)
{
    return cerv_is_digit(c) ||
           (c >= (unsigned char)'A' && c <= (unsigned char)'F') ||
           (c >= (unsigned char)'a' && c <= (unsigned char)'f');
}

static unsigned char cerv_hex_value(unsigned char c)
{
    if (c >= (unsigned char)'0' && c <= (unsigned char)'9') {
        return (unsigned char)(c - (unsigned char)'0');
    }
    if (c >= (unsigned char)'A' && c <= (unsigned char)'F') {
        return (unsigned char)(10U + c - (unsigned char)'A');
    }
    return (unsigned char)(10U + c - (unsigned char)'a');
}

static bool cerv_is_unreserved(unsigned char c)
{
    return cerv_is_alpha(c) || cerv_is_digit(c) || c == (unsigned char)'-' ||
           c == (unsigned char)'.' || c == (unsigned char)'_' || c == (unsigned char)'~';
}

static bool cerv_is_subdelim(unsigned char c)
{
    switch (c) {
    case '!': case '$': case '&': case '\'': case '(': case ')': case '*':
    case '+': case ',': case ';': case '=':
        return true;
    default:
        return false;
    }
}

static bool cerv_pct_valid_at(struct cerv_span s, size_t i)
{
    return i + 2U < s.len && cerv_is_hexdig(s.ptr[i + 1U]) && cerv_is_hexdig(s.ptr[i + 2U]);
}

static bool cerv_path_char_valid(unsigned char c)
{
    return cerv_is_unreserved(c) || cerv_is_subdelim(c) || c == (unsigned char)':' || c == (unsigned char)'@' || c == (unsigned char)'/';
}

static bool cerv_query_char_valid(unsigned char c)
{
    return cerv_path_char_valid(c) || c == (unsigned char)'?';
}

static bool cerv_component_valid(struct cerv_span s, bool query)
{
    size_t i = 0U;
    for (i = 0U; i < s.len; ++i) {
        unsigned char c = s.ptr[i];
        if (c == (unsigned char)'#' || c <= 0x20U || c == 0x7fU || c >= 0x80U) {
            return false;
        }
        if (c == (unsigned char)'%') {
            if (!cerv_pct_valid_at(s, i)) {
                return false;
            }
            i += 2U;
            continue;
        }
        if (!(query ? cerv_query_char_valid(c) : cerv_path_char_valid(c))) {
            return false;
        }
    }
    return true;
}

static bool cerv_scheme_equal(struct cerv_span scheme, const char *literal)
{
    size_t literal_len = strlen(literal);
    size_t i = 0U;
    if (scheme.len != literal_len) return false;
    for (i = 0U; i < scheme.len; ++i) {
        unsigned char a = scheme.ptr[i];
        unsigned char b = (unsigned char)literal[i];
        if (a >= (unsigned char)'A' && a <= (unsigned char)'Z') a = (unsigned char)(a + 32U);
        if (a != b) return false;
    }
    return true;
}

static enum cerv_target_result cerv_parse_origin(struct cerv_span raw, struct cerv_http_target *out)
{
    size_t q = 0U;
    if (raw.len == 0U || raw.ptr[0] != (unsigned char)'/') {
        return CERV_TARGET_BAD_REQUEST;
    }
    while (q < raw.len && raw.ptr[q] != (unsigned char)'?') {
        ++q;
    }
    out->path = (struct cerv_span){.ptr = raw.ptr, .len = q};
    if (q < raw.len) {
        out->has_query = true;
        out->query = (struct cerv_span){.ptr = raw.ptr + q + 1U, .len = raw.len - q - 1U};
    }
    if (!cerv_component_valid(out->path, false) || (out->has_query && !cerv_component_valid(out->query, true))) {
        return CERV_TARGET_BAD_REQUEST;
    }
    return CERV_TARGET_OK;
}

static enum cerv_target_result cerv_parse_absolute(struct cerv_span raw, struct cerv_http_target *out)
{
    size_t colon = 0U;
    size_t pos = 0U;
    size_t authority_end = 0U;
    size_t q = 0U;
    struct cerv_span scheme;
    struct cerv_authority authority;

    if (raw.len < 7U || !cerv_is_alpha(raw.ptr[0])) {
        return CERV_TARGET_BAD_REQUEST;
    }
    colon = 1U;
    while (colon < raw.len && raw.ptr[colon] != (unsigned char)':') {
        unsigned char c = raw.ptr[colon];
        if (!(cerv_is_alpha(c) || cerv_is_digit(c) || c == (unsigned char)'+' || c == (unsigned char)'-' || c == (unsigned char)'.')) {
            return CERV_TARGET_BAD_REQUEST;
        }
        ++colon;
    }
    if (colon >= raw.len) return CERV_TARGET_BAD_REQUEST;
    scheme = (struct cerv_span){.ptr = raw.ptr, .len = colon};
    if (!(cerv_scheme_equal(scheme, "http") || cerv_scheme_equal(scheme, "https"))) {
        return CERV_TARGET_BAD_REQUEST;
    }
    pos = colon + 1U;
    if (pos + 2U > raw.len || raw.ptr[pos] != (unsigned char)'/' || raw.ptr[pos + 1U] != (unsigned char)'/') {
        return CERV_TARGET_BAD_REQUEST;
    }
    pos += 2U;
    authority_end = pos;
    while (authority_end < raw.len && raw.ptr[authority_end] != (unsigned char)'/' && raw.ptr[authority_end] != (unsigned char)'?' && raw.ptr[authority_end] != (unsigned char)'#') {
        ++authority_end;
    }
    out->authority = (struct cerv_span){.ptr = raw.ptr + pos, .len = authority_end - pos};
    if (!cerv_http_authority_parse(out->authority, false, &authority)) {
        return CERV_TARGET_BAD_REQUEST;
    }
    if (authority_end < raw.len && raw.ptr[authority_end] == (unsigned char)'#') {
        return CERV_TARGET_BAD_REQUEST;
    }
    if (authority_end == raw.len || raw.ptr[authority_end] == (unsigned char)'?') {
        out->path_is_implicit_root = true;
        out->path = (struct cerv_span){.ptr = raw.ptr + authority_end, .len = 0U};
        if (authority_end < raw.len) {
            out->has_query = true;
            out->query = (struct cerv_span){.ptr = raw.ptr + authority_end + 1U, .len = raw.len - authority_end - 1U};
        }
    } else {
        q = authority_end;
        while (q < raw.len && raw.ptr[q] != (unsigned char)'?') {
            if (raw.ptr[q] == (unsigned char)'#') return CERV_TARGET_BAD_REQUEST;
            ++q;
        }
        out->path = (struct cerv_span){.ptr = raw.ptr + authority_end, .len = q - authority_end};
        if (q < raw.len) {
            out->has_query = true;
            out->query = (struct cerv_span){.ptr = raw.ptr + q + 1U, .len = raw.len - q - 1U};
        }
    }
    if ((!out->path_is_implicit_root && !cerv_component_valid(out->path, false)) ||
        (out->has_query && !cerv_component_valid(out->query, true))) {
        return CERV_TARGET_BAD_REQUEST;
    }
    return CERV_TARGET_OK;
}

enum cerv_target_result cerv_http_target_parse(struct cerv_span raw, struct cerv_http_target *out)
{
    struct cerv_http_target parsed = {0};
    size_t i = 0U;

    if (out == NULL || raw.ptr == NULL || raw.len == 0U) {
        return CERV_TARGET_BAD_REQUEST;
    }
    if (raw.len > CERV_REQUEST_LINE_MAX) {
        return CERV_TARGET_TOO_LONG;
    }
    parsed.raw = raw;
    if (raw.len == 1U && raw.ptr[0] == (unsigned char)'*') {
        parsed.form = CERV_TARGET_ASTERISK;
        *out = parsed;
        return CERV_TARGET_OK;
    }
    if (raw.ptr[0] == (unsigned char)'/') {
        parsed.form = CERV_TARGET_ORIGIN;
        if (cerv_parse_origin(raw, &parsed) != CERV_TARGET_OK) return CERV_TARGET_BAD_REQUEST;
        *out = parsed;
        return CERV_TARGET_OK;
    }
    for (i = 0U; i < raw.len; ++i) {
        if (raw.ptr[i] == (unsigned char)':') {
            if (i + 2U < raw.len && raw.ptr[i + 1U] == (unsigned char)'/' && raw.ptr[i + 2U] == (unsigned char)'/') {
                parsed.form = CERV_TARGET_ABSOLUTE;
                if (cerv_parse_absolute(raw, &parsed) != CERV_TARGET_OK) return CERV_TARGET_BAD_REQUEST;
                *out = parsed;
                return CERV_TARGET_OK;
            }
            break;
        }
        if (raw.ptr[i] == (unsigned char)'/' || raw.ptr[i] == (unsigned char)'?' || raw.ptr[i] == (unsigned char)'#') {
            break;
        }
    }
    parsed.form = CERV_TARGET_AUTHORITY;
    parsed.authority = raw;
    {
        struct cerv_authority authority;
        if (!cerv_http_authority_parse(raw, false, &authority) || !authority.has_port || authority.port.len == 0U) {
            return CERV_TARGET_BAD_REQUEST;
        }
    }
    *out = parsed;
    return CERV_TARGET_OK;
}

static bool cerv_segment_is_dot(const unsigned char *p, size_t len)
{
    return (len == 1U && p[0] == (unsigned char)'.') ||
           (len == 2U && p[0] == (unsigned char)'.' && p[1] == (unsigned char)'.');
}

enum cerv_target_result cerv_http_target_decode_path(const struct cerv_http_target *target, struct cerv_path *out)
{
    struct cerv_span path;
    size_t in_pos = 0U;
    size_t out_pos = 0U;
    size_t segment_start = 0U;
    bool trailing_slash = false;

    if (target == NULL || out == NULL) return CERV_TARGET_BAD_REQUEST;
    *out = (struct cerv_path){0};
    if (!(target->form == CERV_TARGET_ORIGIN || target->form == CERV_TARGET_ABSOLUTE)) {
        return CERV_TARGET_BAD_REQUEST;
    }
    if (target->path_is_implicit_root) {
        path = (struct cerv_span){.ptr = (const unsigned char *)"/", .len = 1U};
    } else {
        path = target->path;
    }
    if (path.len == 0U || path.ptr[0] != (unsigned char)'/') return CERV_TARGET_BAD_REQUEST;
    trailing_slash = path.ptr[path.len - 1U] == (unsigned char)'/';
    in_pos = 1U;
    segment_start = 0U;
    while (in_pos < path.len) {
        unsigned char c = path.ptr[in_pos];
        if (c == (unsigned char)'/') {
            if (out_pos == segment_start) return CERV_TARGET_BAD_REQUEST;
            if (cerv_segment_is_dot(out->bytes + segment_start, out_pos - segment_start)) return CERV_TARGET_BAD_REQUEST;
            if (out_pos >= CERV_PATH_BYTES_MAX - 1U) return CERV_TARGET_TOO_LONG;
            out->bytes[out_pos++] = (unsigned char)'/';
            ++in_pos;
            segment_start = out_pos;
            continue;
        }
        if (c == (unsigned char)'\\' || c == (unsigned char)'#') return CERV_TARGET_BAD_REQUEST;
        if (c == (unsigned char)'%') {
            unsigned char decoded = 0U;
            if (!cerv_pct_valid_at(path, in_pos)) return CERV_TARGET_BAD_REQUEST;
            decoded = (unsigned char)((unsigned char)(cerv_hex_value(path.ptr[in_pos + 1U]) << 4U) |
                                      cerv_hex_value(path.ptr[in_pos + 2U]));
            if (decoded == (unsigned char)'/' || decoded == (unsigned char)'\\' || decoded == 0U ||
                decoded < 0x20U || decoded == 0x7fU) {
                return CERV_TARGET_BAD_REQUEST;
            }
            c = decoded;
            in_pos += 3U;
        } else {
            ++in_pos;
        }
        if (out_pos >= CERV_PATH_BYTES_MAX - 1U) return CERV_TARGET_TOO_LONG;
        out->bytes[out_pos++] = c;
    }
    if (out_pos > segment_start && cerv_segment_is_dot(out->bytes + segment_start, out_pos - segment_start)) {
        return CERV_TARGET_BAD_REQUEST;
    }
    if (path.len > 1U && trailing_slash && out_pos > 0U && out->bytes[out_pos - 1U] == (unsigned char)'/') {
        --out_pos;
    }
    if (trailing_slash || path.len == 1U) {
        size_t need_sep = out_pos == 0U ? 0U : 1U;
        if (out_pos + need_sep + CERV_INDEX_NAME_LEN >= CERV_PATH_BYTES_MAX) return CERV_TARGET_TOO_LONG;
        if (need_sep != 0U) out->bytes[out_pos++] = (unsigned char)'/';
        memcpy(out->bytes + out_pos, CERV_INDEX_NAME, CERV_INDEX_NAME_LEN);
        out_pos += CERV_INDEX_NAME_LEN;
    }
    out->bytes[out_pos] = 0U;
    out->len = out_pos;
    return CERV_TARGET_OK;
}
