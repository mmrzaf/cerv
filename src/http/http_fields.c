#include "http/http_fields.h"

#include "base/checked.h"
#include "base/bounds.h"

#include <arpa/inet.h>
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

static bool cerv_reg_name_valid(struct cerv_span in)
{
    size_t i = 0U;
    while (i < in.len) {
        unsigned char c = in.ptr[i];
        if (cerv_is_unreserved(c) || cerv_is_subdelim(c)) {
            ++i;
            continue;
        }
        if (c == (unsigned char)'%' && i + 2U < in.len &&
            cerv_is_hexdig(in.ptr[i + 1U]) && cerv_is_hexdig(in.ptr[i + 2U])) {
            i += 3U;
            continue;
        }
        return false;
    }
    return true;
}

static bool cerv_ipvfuture_valid(struct cerv_span in)
{
    size_t i = 0U;
    if (in.len < 4U || (in.ptr[0] != (unsigned char)'v' && in.ptr[0] != (unsigned char)'V')) {
        return false;
    }
    i = 1U;
    while (i < in.len && cerv_is_hexdig(in.ptr[i])) {
        ++i;
    }
    if (i == 1U || i >= in.len || in.ptr[i] != (unsigned char)'.') {
        return false;
    }
    ++i;
    if (i == in.len) {
        return false;
    }
    for (; i < in.len; ++i) {
        unsigned char c = in.ptr[i];
        if (!(cerv_is_unreserved(c) || cerv_is_subdelim(c) || c == (unsigned char)':')) {
            return false;
        }
    }
    return true;
}

static bool cerv_ipv6_valid(struct cerv_span in)
{
    char text[INET6_ADDRSTRLEN + 1U];
    struct in6_addr addr;
    if (in.len == 0U || in.len > (size_t)INET6_ADDRSTRLEN) {
        return false;
    }
    memcpy(text, in.ptr, in.len);
    text[in.len] = '\0';
    return inet_pton(AF_INET6, text, &addr) == 1;
}

bool cerv_http_is_token(struct cerv_span in)
{
    size_t i = 0U;
    if (in.ptr == NULL || in.len == 0U || in.len > CERV_FIELD_LINE_MAX) {
        return false;
    }
    for (i = 0U; i < in.len; ++i) {
        unsigned char c = in.ptr[i];
        if (cerv_is_alpha(c) || cerv_is_digit(c)) {
            continue;
        }
        switch (c) {
        case '!': case '#': case '$': case '%': case '&': case '\'': case '*':
        case '+': case '-': case '.': case '^': case '_': case '`': case '|': case '~':
            break;
        default:
            return false;
        }
    }
    return true;
}

bool cerv_http_field_value_valid(struct cerv_span in)
{
    size_t i = 0U;
    if ((in.ptr == NULL && in.len != 0U) || in.len > CERV_FIELD_LINE_MAX) {
        return false;
    }
    for (i = 0U; i < in.len; ++i) {
        unsigned char c = in.ptr[i];
        if (c == (unsigned char)'\t' || c == (unsigned char)' ') {
            continue;
        }
        if (c >= 0x21U && c <= 0x7eU) {
            continue;
        }
        if (c >= 0x80U) {
            continue;
        }
        return false;
    }
    return true;
}

bool cerv_http_authority_parse(struct cerv_span in, bool allow_empty_host, struct cerv_authority *out)
{
    struct cerv_authority parsed = {0};
    size_t host_end = 0U;
    size_t i = 0U;

    if (out == NULL || (in.ptr == NULL && in.len != 0U) || in.len > CERV_FIELD_LINE_MAX) {
        return false;
    }
    if (in.len == 0U) {
        if (!allow_empty_host) {
            return false;
        }
        parsed.host = in;
        *out = parsed;
        return true;
    }
    for (i = 0U; i < in.len; ++i) {
        if (in.ptr[i] == (unsigned char)'@') {
            return false;
        }
    }
    if (in.ptr[0] == (unsigned char)'[') {
        size_t close = 1U;
        while (close < in.len && in.ptr[close] != (unsigned char)']') {
            ++close;
        }
        if (close >= in.len) {
            return false;
        }
        parsed.host = (struct cerv_span){.ptr = in.ptr + 1U, .len = close - 1U};
        parsed.ip_literal = true;
        if (!(cerv_ipv6_valid(parsed.host) || cerv_ipvfuture_valid(parsed.host))) {
            return false;
        }
        host_end = close + 1U;
    } else {
        host_end = 0U;
        while (host_end < in.len && in.ptr[host_end] != (unsigned char)':') {
            ++host_end;
        }
        parsed.host = (struct cerv_span){.ptr = in.ptr, .len = host_end};
        if (parsed.host.len == 0U && !allow_empty_host) {
            return false;
        }
        if (!cerv_reg_name_valid(parsed.host)) {
            return false;
        }
    }
    if (host_end < in.len) {
        if (in.ptr[host_end] != (unsigned char)':') {
            return false;
        }
        parsed.has_port = true;
        parsed.port = (struct cerv_span){.ptr = in.ptr + host_end + 1U, .len = in.len - host_end - 1U};
        for (i = 0U; i < parsed.port.len; ++i) {
            if (!cerv_is_digit(parsed.port.ptr[i])) {
                return false;
            }
        }
    }
    *out = parsed;
    return true;
}

bool cerv_http_content_length_parse(struct cerv_span in, uint64_t *out)
{
    if ((in.ptr == NULL && in.len != 0U) || in.len > CERV_FIELD_LINE_MAX) return false;
    in = cerv_span_trim_ows(in);
    return cerv_u64_decimal(in.ptr, in.len, out);
}

enum cerv_q_parse_result cerv_http_qvalue_parse(struct cerv_span in, uint16_t *out)
{
    uint16_t value = 0U;
    size_t digits = 0U;
    size_t i = 0U;

    if (out == NULL || in.ptr == NULL || in.len == 0U) {
        return CERV_Q_INVALID;
    }
    if (in.ptr[0] == (unsigned char)'0') {
        value = 0U;
    } else if (in.ptr[0] == (unsigned char)'1') {
        value = 1000U;
    } else {
        return CERV_Q_INVALID;
    }
    if (in.len == 1U) {
        *out = value;
        return CERV_Q_OK;
    }
    if (in.ptr[1] != (unsigned char)'.' || in.len > 5U) {
        return CERV_Q_INVALID;
    }
    digits = in.len - 2U;
    if (in.ptr[0] == (unsigned char)'1') {
        for (i = 0U; i < digits; ++i) {
            if (in.ptr[2U + i] != (unsigned char)'0') {
                return CERV_Q_INVALID;
            }
        }
        *out = 1000U;
        return CERV_Q_OK;
    }
    value = 0U;
    for (i = 0U; i < digits; ++i) {
        if (!cerv_is_digit(in.ptr[2U + i])) {
            return CERV_Q_INVALID;
        }
        if (i == 0U) {
            value = (uint16_t)(value + (uint16_t)(in.ptr[2U + i] - (unsigned char)'0') * 100U);
        } else if (i == 1U) {
            value = (uint16_t)(value + (uint16_t)(in.ptr[2U + i] - (unsigned char)'0') * 10U);
        } else {
            value = (uint16_t)(value + (uint16_t)(in.ptr[2U + i] - (unsigned char)'0'));
        }
    }
    *out = value;
    return CERV_Q_OK;
}

void cerv_accept_encoding_init(struct cerv_accept_encoding *out)
{
    if (out != NULL) {
        *out = (struct cerv_accept_encoding){0};
    }
}

static bool cerv_ae_store(struct cerv_accept_encoding *state, struct cerv_span coding, uint16_t q)
{
    bool *seen = NULL;
    uint16_t *slot = NULL;

    if (cerv_span_equal_ascii_ci(coding, "br")) {
        seen = &state->br_seen;
        slot = &state->br_q;
    } else if (cerv_span_equal_ascii_ci(coding, "gzip")) {
        seen = &state->gzip_seen;
        slot = &state->gzip_q;
    } else if (cerv_span_equal_ascii_ci(coding, "identity")) {
        seen = &state->identity_seen;
        slot = &state->identity_q;
    } else if (coding.len == 1U && coding.ptr[0] == (unsigned char)'*') {
        seen = &state->wildcard_seen;
        slot = &state->wildcard_q;
    } else {
        return true;
    }
    /* Duplicate codings are legal list syntax. Use the highest preference deterministically. */
    if (!*seen || q > *slot) {
        *slot = q;
    }
    *seen = true;
    return true;
}

bool cerv_accept_encoding_add_field(struct cerv_accept_encoding *state, struct cerv_span value)
{
    struct cerv_accept_encoding parsed;
    size_t pos = 0U;
    if (state == NULL || (value.ptr == NULL && value.len != 0U) || value.len > CERV_FIELD_LINE_MAX) {
        return false;
    }
    parsed = *state;
    parsed.present = true;
    value = cerv_span_trim_ows(value);
    if (value.len == 0U) {
        *state = parsed;
        return true;
    }
    for (;;) {
        size_t item_start = pos;
        size_t item_end = pos;
        size_t semi = 0U;
        struct cerv_span item;
        struct cerv_span coding;
        uint16_t q = 1000U;
        bool q_seen = false;

        while (item_end < value.len && value.ptr[item_end] != (unsigned char)',') {
            ++item_end;
        }
        item = cerv_span_trim_ows((struct cerv_span){.ptr = value.ptr + item_start, .len = item_end - item_start});
        if (item.len == 0U) {
            if (item_end == value.len) break;
            pos = item_end + 1U;
            continue;
        }
        semi = 0U;
        while (semi < item.len && item.ptr[semi] != (unsigned char)';') {
            ++semi;
        }
        coding = cerv_span_trim_ows((struct cerv_span){.ptr = item.ptr, .len = semi});
        if (!(cerv_http_is_token(coding) || (coding.len == 1U && coding.ptr[0] == (unsigned char)'*'))) {
            return false;
        }
        while (semi < item.len) {
            size_t param_start = semi + 1U;
            size_t param_end = param_start;
            struct cerv_span param;
            struct cerv_span name;
            struct cerv_span pvalue;
            size_t eq = 0U;

            while (param_end < item.len && item.ptr[param_end] != (unsigned char)';') {
                ++param_end;
            }
            param = cerv_span_trim_ows((struct cerv_span){.ptr = item.ptr + param_start, .len = param_end - param_start});
            eq = 0U;
            while (eq < param.len && param.ptr[eq] != (unsigned char)'=') {
                ++eq;
            }
            if (eq == 0U || eq >= param.len) {
                return false;
            }
            name = (struct cerv_span){.ptr = param.ptr, .len = eq};
            pvalue = (struct cerv_span){.ptr = param.ptr + eq + 1U, .len = param.len - eq - 1U};
            if (!cerv_span_equal_ascii_ci(name, "q") || q_seen || cerv_http_qvalue_parse(pvalue, &q) != CERV_Q_OK) {
                return false;
            }
            q_seen = true;
            semi = param_end;
        }
        if (!cerv_ae_store(&parsed, coding, q)) {
            return false;
        }
        if (item_end == value.len) break;
        pos = item_end + 1U;
    }
    *state = parsed;
    return true;
}

uint16_t cerv_accept_encoding_quality(const struct cerv_accept_encoding *state, const char *coding)
{
    if (state == NULL || coding == NULL) {
        return 0U;
    }
    if (!state->present) {
        return 1000U;
    }
    if (strcmp(coding, "br") == 0) {
        if (state->br_seen) return state->br_q;
        if (state->wildcard_seen) return state->wildcard_q;
        return 0U;
    }
    if (strcmp(coding, "gzip") == 0) {
        if (state->gzip_seen) return state->gzip_q;
        if (state->wildcard_seen) return state->wildcard_q;
        return 0U;
    }
    if (strcmp(coding, "identity") == 0) {
        if (state->identity_seen) return state->identity_q;
        if (state->wildcard_seen && state->wildcard_q == 0U) return 0U;
        return 1000U;
    }
    if (state->wildcard_seen) {
        return state->wildcard_q;
    }
    return 0U;
}

enum cerv_etag_parse_result cerv_entity_tag_parse(struct cerv_span in, struct cerv_entity_tag *out)
{
    size_t pos = 0U;
    struct cerv_entity_tag tag = {0};
    if ((in.ptr == NULL && in.len != 0U) || in.len > CERV_FIELD_LINE_MAX) return CERV_ETAG_INVALID;
    in = cerv_span_trim_ows(in);
    if (out == NULL || in.ptr == NULL || in.len < 2U) {
        return CERV_ETAG_INVALID;
    }
    if (in.len >= 4U && in.ptr[0] == (unsigned char)'W' && in.ptr[1] == (unsigned char)'/') {
        tag.weak = true;
        pos = 2U;
    }
    if (in.len - pos < 2U || in.ptr[pos] != (unsigned char)'"' || in.ptr[in.len - 1U] != (unsigned char)'"') {
        return CERV_ETAG_INVALID;
    }
    tag.opaque = (struct cerv_span){.ptr = in.ptr + pos + 1U, .len = in.len - pos - 2U};
    for (size_t i = 0U; i < tag.opaque.len; ++i) {
        unsigned char c = tag.opaque.ptr[i];
        if (!(c == 0x21U || (c >= 0x23U && c <= 0x7eU) || c >= 0x80U)) {
            return CERV_ETAG_INVALID;
        }
    }
    *out = tag;
    return CERV_ETAG_OK;
}

bool cerv_entity_tag_weak_equal(struct cerv_entity_tag a, struct cerv_entity_tag b)
{
    return cerv_span_equal(a.opaque, b.opaque);
}

bool cerv_entity_tag_strong_equal(struct cerv_entity_tag a, struct cerv_entity_tag b)
{
    return !a.weak && !b.weak && cerv_span_equal(a.opaque, b.opaque);
}

static bool cerv_if_none_match_each(struct cerv_span in, struct cerv_entity_tag *current, bool *matched)
{
    size_t pos = 0U;
    if ((in.ptr == NULL && in.len != 0U) || in.len > CERV_FIELD_LINE_MAX) return false;
    in = cerv_span_trim_ows(in);
    if (in.len == 1U && in.ptr[0] == (unsigned char)'*') {
        if (matched != NULL) *matched = true;
        return true;
    }
    if (in.len == 0U) {
        return true;
    }
    for (;;) {
        size_t end = pos;
        struct cerv_span item;
        struct cerv_entity_tag tag;
        while (end < in.len && in.ptr[end] != (unsigned char)',') {
            ++end;
        }
        item = cerv_span_trim_ows((struct cerv_span){.ptr = in.ptr + pos, .len = end - pos});
        if (item.len != 0U) {
            if (cerv_entity_tag_parse(item, &tag) != CERV_ETAG_OK) {
                return false;
            }
            if (matched != NULL && current != NULL && cerv_entity_tag_weak_equal(tag, *current)) {
                *matched = true;
            }
        }
        if (end == in.len) break;
        pos = end + 1U;
    }
    return true;
}

bool cerv_if_none_match_valid(struct cerv_span in)
{
    return cerv_if_none_match_each(in, NULL, NULL);
}

bool cerv_if_none_match_matches(struct cerv_span in, struct cerv_entity_tag current)
{
    bool matched = false;
    if (!cerv_if_none_match_each(in, &current, &matched)) {
        return false;
    }
    return matched;
}


enum cerv_if_range_parse_result cerv_http_if_range_parse(struct cerv_span in, int current_year,
                                                          struct cerv_if_range_value *out)
{
    struct cerv_if_range_value parsed = {0};
    struct cerv_entity_tag tag;
    struct cerv_http_date date;

    if (out == NULL || (in.ptr == NULL && in.len != 0U) || in.len > CERV_FIELD_LINE_MAX) {
        return CERV_IF_RANGE_INVALID;
    }
    in = cerv_span_trim_ows(in);
    if (cerv_entity_tag_parse(in, &tag) == CERV_ETAG_OK) {
        parsed.kind = CERV_IF_RANGE_ETAG;
        parsed.etag = tag;
        *out = parsed;
        return CERV_IF_RANGE_ETAG;
    }
    if (cerv_http_date_parse(in, current_year, &date)) {
        parsed.kind = CERV_IF_RANGE_DATE;
        parsed.date = date;
        *out = parsed;
        return CERV_IF_RANGE_DATE;
    }
    return CERV_IF_RANGE_INVALID;
}
