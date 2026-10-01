#include "base/bounds.h"
#include "base/buffer.h"
#include "base/checked.h"
#include "base/path.h"
#include "http/http_date.h"
#include "http/http_fields.h"
#include "http/http_range.h"
#include "http/http_request.h"
#include "http/http_target.h"
#include "base/slice.h"
#include "base/cerv_time.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned tests_run = 0U;
static unsigned tests_failed = 0U;

#define CHECK(expr) do { \
    ++tests_run; \
    if (!(expr)) { \
        ++tests_failed; \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
    } \
} while (0)

static struct cerv_span span(const char *s)
{
    return (struct cerv_span){.ptr = (const unsigned char *)s, .len = strlen(s)};
}

static bool accept_encoding_equal(const struct cerv_accept_encoding *a,
                                  const struct cerv_accept_encoding *b)
{
    return a->present == b->present && a->br_seen == b->br_seen &&
           a->gzip_seen == b->gzip_seen && a->identity_seen == b->identity_seen &&
           a->wildcard_seen == b->wildcard_seen && a->br_q == b->br_q &&
           a->gzip_q == b->gzip_q && a->identity_q == b->identity_q &&
           a->wildcard_q == b->wildcard_q;
}

static void test_checked_buffer_time(void)
{
    size_t sz = 0U;
    uint64_t u = UINT64_C(0);
    unsigned char storage[8];
    struct cerv_buffer b;
    struct cerv_duration d;
    struct cerv_mono_time t;

    CHECK(cerv_size_add(1U, 2U, &sz) && sz == 3U);
    CHECK(!cerv_size_add(SIZE_MAX, 1U, &sz));
    CHECK(cerv_size_sub(3U, 2U, &sz) && sz == 1U);
    CHECK(!cerv_size_sub(2U, 3U, &sz));
    CHECK(cerv_size_mul(3U, 4U, &sz) && sz == 12U);
    CHECK(cerv_size_mul(0U, SIZE_MAX, &sz) && sz == 0U);
    CHECK(!cerv_size_mul(SIZE_MAX, 2U, &sz));
    CHECK(cerv_u64_add(UINT64_C(1), UINT64_C(2), &u) && u == UINT64_C(3));
    CHECK(!cerv_u64_add(UINT64_MAX, UINT64_C(1), &u));
    CHECK(cerv_u64_sub(UINT64_C(3), UINT64_C(2), &u) && u == UINT64_C(1));
    CHECK(!cerv_u64_sub(UINT64_C(2), UINT64_C(3), &u));
    CHECK(cerv_u64_mul(UINT64_C(3), UINT64_C(4), &u) && u == UINT64_C(12));
    CHECK(cerv_u64_mul(UINT64_C(0), UINT64_MAX, &u) && u == UINT64_C(0));
    CHECK(!cerv_u64_mul(UINT64_MAX, UINT64_C(2), &u));
    CHECK(cerv_u64_decimal((const unsigned char *)"18446744073709551615", 20U, &u) && u == UINT64_MAX);
    CHECK(!cerv_u64_decimal((const unsigned char *)"18446744073709551616", 20U, &u));
    CHECK(!cerv_u64_decimal((const unsigned char *)"12x", 3U, &u));
    CHECK(!cerv_u64_decimal((const unsigned char *)"000000000000000000000", 21U, &u));
    CHECK(!cerv_u64_decimal(NULL, 1U, &u));
    CHECK(!cerv_u64_decimal((const unsigned char *)"1", 1U, NULL));
    {
        off_t off = (off_t)-1;
        CHECK(cerv_u64_to_off_t((uint64_t)INT64_MAX, &off) && off == (off_t)INT64_MAX);
        CHECK(!cerv_u64_to_off_t((uint64_t)INT64_MAX + UINT64_C(1), &off));
    }
    CHECK(cerv_span_trim_ows((struct cerv_span){.ptr = NULL, .len = 0U}).ptr == NULL);

    cerv_buffer_init(&b, storage, sizeof(storage));
    CHECK(cerv_buffer_append(&b, "abc", 3U));
    CHECK(cerv_buffer_append_byte(&b, (unsigned char)'!'));
    CHECK(cerv_buffer_append_u64(&b, UINT64_C(42)));
    CHECK(b.used == 6U && memcmp(storage, "abc!42", 6U) == 0);
    CHECK(!cerv_buffer_append(&b, "xxx", 3U));
    CHECK(b.used == 6U);
    CHECK(cerv_buffer_append(&b, NULL, 0U));
    CHECK(!cerv_buffer_append(&b, NULL, 1U));
    {
        struct cerv_buffer zero;
        cerv_buffer_init(&zero, NULL, 0U);
        CHECK(cerv_buffer_append(&zero, NULL, 0U) && zero.used == 0U);
        CHECK(!cerv_buffer_append_byte(&zero, (unsigned char)'x'));
    }
    {
        unsigned char overlap[8] = {'a', 'b', 'c', 'd', 'e', 'f', 0U, 0U};
        struct cerv_buffer overlapping;
        cerv_buffer_init(&overlapping, overlap, sizeof(overlap));
        overlapping.used = 2U;
        CHECK(cerv_buffer_append(&overlapping, overlap + 1U, 4U));
        CHECK(overlapping.used == 6U && memcmp(overlap, "abbcde", 6U) == 0);
    }

    CHECK(cerv_duration_from_ms(UINT64_C(5), &d) && d.ns == UINT64_C(5000000));
    CHECK(cerv_duration_from_ms(UINT64_MAX / UINT64_C(1000000), &d));
    CHECK(!cerv_duration_from_ms(UINT64_MAX / UINT64_C(1000000) + UINT64_C(1), &d));
    CHECK(cerv_duration_from_seconds(UINT64_C(5), &d) && d.ns == UINT64_C(5000000000));
    CHECK(!cerv_duration_from_seconds(UINT64_MAX / UINT64_C(1000000000) + UINT64_C(1), &d));
    t = cerv_mono_add_saturating((struct cerv_mono_time){.ns = UINT64_MAX - 1U}, d);
    CHECK(t.ns == UINT64_MAX);
    CHECK(cerv_mono_remaining_ms_ceil((struct cerv_mono_time){.ns = 1U},
                                      (struct cerv_mono_time){.ns = 1000002U}) == UINT64_C(2));
    CHECK(cerv_mono_remaining_ms_ceil((struct cerv_mono_time){.ns = 100U},
                                      (struct cerv_mono_time){.ns = 100U}) == UINT64_C(0));
    CHECK(cerv_mono_remaining_ms_ceil((struct cerv_mono_time){.ns = 1000000U},
                                      (struct cerv_mono_time){.ns = 2000000U}) == UINT64_C(1));
}

static void test_fields(void)
{
    struct cerv_authority a;
    uint64_t cl = UINT64_C(99);
    uint16_t q = 0U;
    struct cerv_accept_encoding ae;
    struct cerv_entity_tag weak;
    struct cerv_entity_tag strong;
    struct cerv_if_range_value if_range;

    CHECK(cerv_http_is_token(span("X-Test_1")));
    CHECK(!cerv_http_is_token(span("bad name")));
    CHECK(cerv_http_field_value_valid(span(" hello\tworld ")));
    {
        const unsigned char bad[] = {'a', 0U, 'b'};
        CHECK(!cerv_http_field_value_valid((struct cerv_span){.ptr = bad, .len = sizeof(bad)}));
    }
    CHECK(cerv_http_authority_parse(span("example.com:8080"), false, &a) && a.has_port && a.port.len == 4U);
    CHECK(cerv_http_authority_parse(span("[::1]:80"), false, &a) && a.ip_literal);
    CHECK(cerv_http_authority_parse(span("[v1.a:b]:443"), false, &a) && a.ip_literal && a.has_port);
    CHECK(cerv_http_authority_parse(span("example.com:"), false, &a) && a.has_port && a.port.len == 0U);
    CHECK(cerv_http_authority_parse(span(""), true, &a));
    CHECK(!cerv_http_authority_parse(span("user@example.com"), false, &a));
    CHECK(!cerv_http_authority_parse(span("[:::]:80"), false, &a));
    CHECK(!cerv_http_authority_parse(span("[v.a]:80"), false, &a));
    CHECK(!cerv_http_authority_parse(span("example.com:abc"), false, &a));

    CHECK(cerv_http_content_length_parse(span(" 0\t"), &cl) && cl == UINT64_C(0));
    CHECK(!cerv_http_content_length_parse(span("+0"), &cl));
    CHECK(cerv_http_qvalue_parse(span("0.875"), &q) == CERV_Q_OK && q == 875U);
    CHECK(cerv_http_qvalue_parse(span("1.000"), &q) == CERV_Q_OK && q == 1000U);
    CHECK(cerv_http_qvalue_parse(span("1.001"), &q) == CERV_Q_INVALID);
    CHECK(cerv_http_qvalue_parse(span("0.0000"), &q) == CERV_Q_INVALID);
    CHECK(cerv_http_qvalue_parse(span("0."), &q) == CERV_Q_OK && q == 0U);
    CHECK(cerv_http_qvalue_parse(span("1."), &q) == CERV_Q_OK && q == 1000U);
    CHECK(cerv_http_qvalue_parse(span(" 0.5"), &q) == CERV_Q_INVALID);

    cerv_accept_encoding_init(&ae);
    CHECK(cerv_accept_encoding_quality(&ae, "br") == 1000U);
    CHECK(cerv_accept_encoding_add_field(&ae, span("gzip;q=0.4, br;q=1, identity;q=0")));
    CHECK(cerv_accept_encoding_quality(&ae, "br") == 1000U);
    CHECK(cerv_accept_encoding_quality(&ae, "gzip") == 400U);
    CHECK(cerv_accept_encoding_quality(&ae, "identity") == 0U);
    CHECK(cerv_accept_encoding_add_field(&ae, span("gzip;q=0.8")));
    CHECK(cerv_accept_encoding_quality(&ae, "gzip") == 800U);
    cerv_accept_encoding_init(&ae);
    CHECK(cerv_accept_encoding_add_field(&ae, span("*;q=0.2")));
    CHECK(cerv_accept_encoding_quality(&ae, "br") == 200U);
    CHECK(cerv_accept_encoding_quality(&ae, "identity") == 1000U);
    CHECK(cerv_accept_encoding_quality(&ae, "zstd") == 200U);
    cerv_accept_encoding_init(&ae);
    CHECK(cerv_accept_encoding_add_field(&ae, span("*;q=0")));
    CHECK(cerv_accept_encoding_quality(&ae, "identity") == 0U);
    CHECK(cerv_accept_encoding_quality(&ae, "br") == 0U);
    cerv_accept_encoding_init(&ae);
    CHECK(cerv_accept_encoding_add_field(&ae, span("")));
    CHECK(cerv_accept_encoding_quality(&ae, "identity") == 1000U);
    CHECK(cerv_accept_encoding_quality(&ae, "gzip") == 0U);
    CHECK(!cerv_accept_encoding_add_field(&ae, span("gzip;q=1.0000")));
    cerv_accept_encoding_init(&ae);
    CHECK(cerv_accept_encoding_add_field(&ae, span("identity; q=0.5")));
    CHECK(cerv_accept_encoding_quality(&ae, "identity") == 500U);
    {
        struct cerv_accept_encoding before = ae;
        CHECK(!cerv_accept_encoding_add_field(&ae, span("gzip;q =0.5")));
        CHECK(accept_encoding_equal(&ae, &before));
    }
    CHECK(cerv_accept_encoding_add_field(&ae, span("GZIP;Q=0.25")));
    CHECK(cerv_accept_encoding_quality(&ae, "gzip") == 250U);
    cerv_accept_encoding_init(&ae);
    CHECK(cerv_accept_encoding_add_field(&ae, span(", gzip,, br,  ")));
    CHECK(cerv_accept_encoding_quality(&ae, "gzip") == 1000U);
    CHECK(cerv_accept_encoding_quality(&ae, "br") == 1000U);

    CHECK(cerv_entity_tag_parse(span("W/\"abc\""), &weak) == CERV_ETAG_OK && weak.weak);
    CHECK(cerv_entity_tag_parse(span("\"abc\""), &strong) == CERV_ETAG_OK && !strong.weak);
    CHECK(cerv_entity_tag_weak_equal(weak, strong));
    CHECK(!cerv_entity_tag_strong_equal(weak, strong));
    CHECK(cerv_entity_tag_parse(span("\"\""), &strong) == CERV_ETAG_OK);
    CHECK(cerv_entity_tag_parse(span("w/\"abc\""), &strong) == CERV_ETAG_INVALID);
    CHECK(cerv_entity_tag_parse(span("\"a b\""), &strong) == CERV_ETAG_INVALID);
    CHECK(cerv_entity_tag_parse(span("\"a\\b\""), &strong) == CERV_ETAG_OK);
    CHECK(cerv_entity_tag_parse(span("\"abc\""), &strong) == CERV_ETAG_OK);
    CHECK(cerv_if_none_match_valid(span("W/\"a\", \"b\"")));
    CHECK(cerv_if_none_match_valid(span("*")));
    CHECK(!cerv_if_none_match_valid(span("*, \"a\"")));
    CHECK(cerv_if_none_match_matches(span("\"x\", W/\"abc\""), strong));
    CHECK(cerv_if_none_match_valid(span("\"a\",")));
    CHECK(cerv_if_none_match_valid(span(", , W/\"a\",,")));
    CHECK(cerv_if_none_match_valid(span("")));
    CHECK(!cerv_if_none_match_matches(span(", ,"), strong));
    CHECK(cerv_http_if_range_parse(span("\"abc\""), 2026, &if_range) == CERV_IF_RANGE_ETAG && !if_range.etag.weak);
    CHECK(cerv_http_if_range_parse(span("W/\"abc\""), 2026, &if_range) == CERV_IF_RANGE_ETAG && if_range.etag.weak);
    CHECK(cerv_http_if_range_parse(span("Sun, 06 Nov 1994 08:49:37 GMT"), 2026, &if_range) == CERV_IF_RANGE_DATE);
    CHECK(cerv_http_if_range_parse(span("garbage"), 2026, &if_range) == CERV_IF_RANGE_INVALID);
}

static void check_path(const char *raw, enum cerv_target_result expected, const char *decoded)
{
    struct cerv_http_target t;
    struct cerv_path p;
    enum cerv_target_result r = cerv_http_target_parse(span(raw), &t);
    if (r == CERV_TARGET_OK) r = cerv_http_target_decode_path(&t, &p);
    CHECK(r == expected);
    if (expected == CERV_TARGET_OK && decoded != NULL) {
        CHECK(p.len == strlen(decoded));
        CHECK(memcmp(p.bytes, decoded, p.len) == 0);
        CHECK(p.bytes[p.len] == 0U);
    }
}


static void test_parser_domain_bounds(void)
{
    size_t n = CERV_FIELD_LINE_MAX + 1U;
    unsigned char *bytes = malloc(n);
    struct cerv_authority authority;
    struct cerv_accept_encoding ae;
    struct cerv_accept_encoding before;
    struct cerv_entity_tag tag;
    struct cerv_if_range_value if_range;
    struct cerv_range_spec range;
    struct cerv_http_date date;
    uint64_t content_length = UINT64_C(0);

    CHECK(bytes != NULL);
    if (bytes == NULL) return;

    memset(bytes, 'a', n);
    CHECK(!cerv_http_is_token((struct cerv_span){.ptr = bytes, .len = n}));
    CHECK(!cerv_http_field_value_valid((struct cerv_span){.ptr = bytes, .len = n}));
    CHECK(!cerv_http_authority_parse((struct cerv_span){.ptr = bytes, .len = n}, false, &authority));

    memset(bytes, '0', n);
    CHECK(!cerv_http_content_length_parse((struct cerv_span){.ptr = bytes, .len = n}, &content_length));

    memset(bytes, 'a', n);
    cerv_accept_encoding_init(&ae);
    before = ae;
    CHECK(!cerv_accept_encoding_add_field(&ae, (struct cerv_span){.ptr = bytes, .len = n}));
    CHECK(accept_encoding_equal(&ae, &before));

    bytes[0] = (unsigned char)'"';
    bytes[n - 1U] = (unsigned char)'"';
    CHECK(cerv_entity_tag_parse((struct cerv_span){.ptr = bytes, .len = n}, &tag) == CERV_ETAG_INVALID);
    CHECK(!cerv_if_none_match_valid((struct cerv_span){.ptr = bytes, .len = n}));
    CHECK(cerv_http_if_range_parse((struct cerv_span){.ptr = bytes, .len = n}, 2026, &if_range) == CERV_IF_RANGE_INVALID);

    memset(bytes, ' ', n);
    memcpy(bytes, "bytes=0-", 8U);
    CHECK(cerv_http_range_parse((struct cerv_span){.ptr = bytes, .len = n}, &range) == CERV_RANGE_MALFORMED);

    memset(bytes, 'x', 34U);
    CHECK(!cerv_http_date_parse((struct cerv_span){.ptr = bytes, .len = 34U}, 2026, &date));
    free(bytes);
}

static void test_targets(void)
{
    struct cerv_http_target t;
    struct cerv_path p;
    check_path("/", CERV_TARGET_OK, "index.html");
    check_path("/a", CERV_TARGET_OK, "a");
    check_path("/a/", CERV_TARGET_OK, "a/index.html");
    check_path("/a//b", CERV_TARGET_BAD_REQUEST, NULL);
    check_path("/./a", CERV_TARGET_BAD_REQUEST, NULL);
    check_path("/../a", CERV_TARGET_BAD_REQUEST, NULL);
    check_path("/a/../b", CERV_TARGET_BAD_REQUEST, NULL);
    check_path("/%2e/a", CERV_TARGET_BAD_REQUEST, NULL);
    check_path("/%2e%2e/a", CERV_TARGET_BAD_REQUEST, NULL);
    check_path("/%252e%252e/a", CERV_TARGET_OK, "%2e%2e/a");
    check_path("/a%2fb", CERV_TARGET_BAD_REQUEST, NULL);
    check_path("/a%2Fb", CERV_TARGET_BAD_REQUEST, NULL);
    check_path("/a\\b", CERV_TARGET_BAD_REQUEST, NULL);
    check_path("/a%5cb", CERV_TARGET_BAD_REQUEST, NULL);
    check_path("/%00", CERV_TARGET_BAD_REQUEST, NULL);
    check_path("/%", CERV_TARGET_BAD_REQUEST, NULL);
    check_path("/%0", CERV_TARGET_BAD_REQUEST, NULL);
    check_path("/%GG", CERV_TARGET_BAD_REQUEST, NULL);
    check_path("/#fragment-like-text", CERV_TARGET_BAD_REQUEST, NULL);
    check_path("/?query", CERV_TARGET_OK, "index.html");
    check_path("/caf%C3%A9", CERV_TARGET_OK, "caf\xC3\xA9");

    CHECK(cerv_http_target_parse(span("http://example.com/a?q=1"), &t) == CERV_TARGET_OK &&
          t.form == CERV_TARGET_ABSOLUTE && t.authority.len == strlen("example.com"));
    CHECK(cerv_http_target_decode_path(&t, &p) == CERV_TARGET_OK && p.len == 1U && p.bytes[0] == (unsigned char)'a');
    CHECK(cerv_http_target_parse(span("https://example.com?x"), &t) == CERV_TARGET_OK && t.path_is_implicit_root);
    CHECK(cerv_http_target_decode_path(&t, &p) == CERV_TARGET_OK && memcmp(p.bytes, "index.html", 10U) == 0);
    CHECK(cerv_http_target_parse(span("example.com:443"), &t) == CERV_TARGET_OK && t.form == CERV_TARGET_AUTHORITY);
    CHECK(cerv_http_target_parse(span("*"), &t) == CERV_TARGET_OK && t.form == CERV_TARGET_ASTERISK);
    CHECK(cerv_http_target_parse(span("ftp://example.com/a"), &t) == CERV_TARGET_BAD_REQUEST);
}

static bool hidden(const char *path)
{
    return cerv_path_is_hidden((const unsigned char *)path, strlen(path));
}

static bool last_dot(const char *path)
{
    return cerv_path_last_segment_has_dot((const unsigned char *)path, strlen(path));
}

static void test_path_policy(void)
{
    struct cerv_http_target t;
    struct cerv_path p;

    /* Dotfiles and dot-directories are hidden at any depth; only a leading .well-known is public. */
    CHECK(hidden(".env"));
    CHECK(hidden(".git/HEAD"));
    CHECK(hidden("a/.git/config"));
    CHECK(hidden("a/b/.htpasswd"));
    CHECK(hidden("..env"));
    CHECK(hidden(".well-known2/x"));
    CHECK(hidden(".Well-Known/x"));
    CHECK(hidden("a/.well-known/x"));
    CHECK(hidden(".well-known/.secret"));
    CHECK(hidden(".well-known/a/.secret"));
    CHECK(hidden("x/.well-known"));
    CHECK(!hidden(".well-known/security.txt"));
    CHECK(!hidden(".well-known/acme-challenge/token"));
    CHECK(!hidden(".well-known"));
    CHECK(!hidden("index.html"));
    CHECK(!hidden("a/b.c/d"));
    CHECK(!hidden("a./b"));
    CHECK(!hidden("a/b."));
    CHECK(!hidden("caf\xC3\xA9/x"));
    CHECK(hidden("caf\xC3\xA9/.x"));
    CHECK(!hidden(""));
    CHECK(!cerv_path_is_hidden(NULL, 0U));

    CHECK(last_dot("app.js"));
    CHECK(last_dot("a/b/app.min.js"));
    CHECK(last_dot("a.b/c.d"));
    CHECK(last_dot(".env"));
    CHECK(!last_dot("dashboard"));
    CHECK(!last_dot("a.b/dashboard"));
    CHECK(!last_dot("users/42"));
    CHECK(!last_dot(""));
    CHECK(!cerv_path_last_segment_has_dot(NULL, 0U));

    /* The decoder records whether the index name was synthesized for a directory request. */
    CHECK(cerv_http_target_parse(span("/"), &t) == CERV_TARGET_OK && cerv_http_target_decode_path(&t, &p) == CERV_TARGET_OK && p.directory_index);
    CHECK(cerv_http_target_parse(span("/a/"), &t) == CERV_TARGET_OK && cerv_http_target_decode_path(&t, &p) == CERV_TARGET_OK && p.directory_index);
    CHECK(cerv_http_target_parse(span("/a"), &t) == CERV_TARGET_OK && cerv_http_target_decode_path(&t, &p) == CERV_TARGET_OK && !p.directory_index);
    CHECK(cerv_http_target_parse(span("/a/index.html"), &t) == CERV_TARGET_OK && cerv_http_target_decode_path(&t, &p) == CERV_TARGET_OK && !p.directory_index);

    /* Percent-encoded leading dots are decoded before the policy runs. */
    CHECK(cerv_http_target_parse(span("/%2eenv"), &t) == CERV_TARGET_OK && cerv_http_target_decode_path(&t, &p) == CERV_TARGET_OK &&
          cerv_path_is_hidden(p.bytes, p.len));
    CHECK(cerv_http_target_parse(span("/a/%2Egit/config"), &t) == CERV_TARGET_OK && cerv_http_target_decode_path(&t, &p) == CERV_TARGET_OK &&
          cerv_path_is_hidden(p.bytes, p.len));
    CHECK(cerv_http_target_parse(span("/.well-known/security.txt"), &t) == CERV_TARGET_OK &&
          cerv_http_target_decode_path(&t, &p) == CERV_TARGET_OK && !cerv_path_is_hidden(p.bytes, p.len));
}

static void test_dates(void)
{
    struct cerv_http_date d;
    unsigned char out[29];
    CHECK(cerv_http_date_parse(span("Sun, 06 Nov 1994 08:49:37 GMT"), 2026, &d));
    CHECK(d.year == 1994 && d.month == 11U && d.day == 6U && d.hour == 8U && d.minute == 49U && d.second == 37U);
    CHECK(cerv_http_date_parse(span("Sunday, 06-Nov-94 08:49:37 GMT"), 2026, &d) && d.year == 1994);
    CHECK(cerv_http_date_parse(span("Friday, 06-Nov-76 08:49:37 GMT"), 2026, &d) && d.year == 2076);
    CHECK(cerv_http_date_parse(span("Sunday, 06-Nov-77 08:49:37 GMT"), 2026, &d) && d.year == 1977);
    CHECK(cerv_http_date_parse(span("Sun Nov  6 08:49:37 1994"), 2026, &d) && d.year == 1994);
    CHECK(!cerv_http_date_parse(span("Mon, 06 Nov 1994 08:49:37 GMT"), 2026, &d));
    CHECK(!cerv_http_date_parse(span("Sun, 31 Feb 1994 08:49:37 GMT"), 2026, &d));
    CHECK(!cerv_http_date_parse(span("Sun, 06 Nov 1994 08:49:37 UTC"), 2026, &d));
    CHECK(!cerv_http_date_parse(span("sun, 06 Nov 1994 08:49:37 GMT"), 2026, &d));
    CHECK(!cerv_http_date_parse(span("Sun, 06 Nov 1994 24:00:00 GMT"), 2026, &d));
    CHECK(cerv_http_date_parse(span("Sun, 06 Nov 1994 08:49:60 GMT"), 2026, &d));
    CHECK(cerv_http_date_format_imf(INT64_C(0), out));
    CHECK(memcmp(out, "Thu, 01 Jan 1970 00:00:00 GMT", 29U) == 0);
    CHECK(cerv_http_date_format_imf(INT64_C(-1), out));
    CHECK(memcmp(out, "Wed, 31 Dec 1969 23:59:59 GMT", 29U) == 0);
    CHECK(cerv_http_date_format_imf(INT64_C(784111777), out));
    CHECK(memcmp(out, "Sun, 06 Nov 1994 08:49:37 GMT", 29U) == 0);
    CHECK(cerv_http_date_format_imf(INT64_C(-62167219200), out));
    CHECK(!cerv_http_date_format_imf(INT64_C(-62167219201), out));
    CHECK(cerv_http_date_format_imf(INT64_C(253402300799), out));
    CHECK(!cerv_http_date_format_imf(INT64_C(253402300800), out));
    CHECK(!cerv_http_date_format_imf(INT64_MIN, out));
    CHECK(!cerv_http_date_format_imf(INT64_MAX, out));
}

static void test_ranges(void)
{
    struct cerv_range_spec s;
    struct cerv_range_selection sel;
    CHECK(cerv_http_range_parse(span("bytes=0-0"), &s) == CERV_RANGE_SINGLE && s.kind == CERV_RANGE_START_END);
    CHECK(cerv_http_range_normalize(s, UINT64_C(10), &sel) == CERV_RANGE_SATISFIABLE && sel.start == 0U && sel.end == 0U && sel.count == 1U);
    CHECK(cerv_http_range_parse(span("bytes=0-10"), &s) == CERV_RANGE_SINGLE);
    CHECK(cerv_http_range_normalize(s, UINT64_C(10), &sel) == CERV_RANGE_SATISFIABLE && sel.end == 9U);
    CHECK(cerv_http_range_parse(span("bytes=9-"), &s) == CERV_RANGE_SINGLE);
    CHECK(cerv_http_range_normalize(s, UINT64_C(10), &sel) == CERV_RANGE_SATISFIABLE && sel.start == 9U && sel.end == 9U);
    CHECK(cerv_http_range_parse(span("bytes=10-"), &s) == CERV_RANGE_SINGLE);
    CHECK(cerv_http_range_normalize(s, UINT64_C(10), &sel) == CERV_RANGE_UNSATISFIABLE);
    CHECK(cerv_http_range_parse(span("bytes=-1"), &s) == CERV_RANGE_SINGLE);
    CHECK(cerv_http_range_normalize(s, UINT64_C(10), &sel) == CERV_RANGE_SATISFIABLE && sel.start == 9U);
    CHECK(cerv_http_range_parse(span("bytes=-50"), &s) == CERV_RANGE_SINGLE);
    CHECK(cerv_http_range_normalize(s, UINT64_C(10), &sel) == CERV_RANGE_SATISFIABLE && sel.start == 0U && sel.count == 10U);
    CHECK(cerv_http_range_parse(span("bytes=-0"), &s) == CERV_RANGE_SINGLE);
    CHECK(cerv_http_range_normalize(s, UINT64_C(10), &sel) == CERV_RANGE_UNSATISFIABLE);
    CHECK(cerv_http_range_parse(span("bytes=5-4"), &s) == CERV_RANGE_MALFORMED);
    CHECK(cerv_http_range_parse(span("bytes=18446744073709551616-"), &s) == CERV_RANGE_MALFORMED);
    CHECK(cerv_http_range_parse(span("bytes=0-1, 3-4"), &s) == CERV_RANGE_MULTIPLE);
    CHECK(cerv_http_range_parse(span("bytes=,0-1,,"), &s) == CERV_RANGE_SINGLE && s.kind == CERV_RANGE_START_END);
    CHECK(cerv_http_range_parse(span("bytes= , 0-1"), &s) == CERV_RANGE_SINGLE);
    CHECK(cerv_http_range_parse(span("bytes=0-1,"), &s) == CERV_RANGE_SINGLE);
    CHECK(cerv_http_range_parse(span("bytes=,,,"), &s) == CERV_RANGE_MALFORMED);
    CHECK(cerv_http_range_parse(span("items=0-1"), &s) == CERV_RANGE_UNIT_UNSUPPORTED);
    CHECK(cerv_http_range_parse(span("items=abc, def"), &s) == CERV_RANGE_UNIT_UNSUPPORTED);
    CHECK(cerv_http_range_parse(span("items="), &s) == CERV_RANGE_MALFORMED);
    CHECK(cerv_http_range_parse(span("items= a"), &s) == CERV_RANGE_MALFORMED);
    CHECK(cerv_http_range_parse(span("items=a b"), &s) == CERV_RANGE_MALFORMED);
    CHECK(cerv_http_range_parse(span("bytes =0-1"), &s) == CERV_RANGE_MALFORMED);
    CHECK(cerv_http_range_parse(span("bytes= 0-1"), &s) == CERV_RANGE_MALFORMED);
    CHECK(cerv_http_range_parse(span("bytes=0-1 , 3-4"), &s) == CERV_RANGE_MULTIPLE);
    CHECK(cerv_http_range_normalize((struct cerv_range_spec){.kind = CERV_RANGE_START_END, .first = 5U, .second = 4U}, UINT64_C(10), &sel) == CERV_RANGE_UNSATISFIABLE);
    CHECK(cerv_http_range_normalize((struct cerv_range_spec){.kind = CERV_RANGE_SUFFIX, .first = 1U}, 0U, &sel) == CERV_RANGE_UNSATISFIABLE);
}

static enum cerv_parse_result parse_text(const char *s, struct cerv_http_request *req)
{
    return cerv_http_request_parse((const unsigned char *)s, strlen(s), req);
}

static void test_requests_basic(void)
{
    struct cerv_http_request r;
    struct cerv_entity_tag current;
    CHECK(cerv_http_request_line_parse(span("GET / HTTP/1.1"), &r) == CERV_PARSE_OK && r.method == CERV_METHOD_GET);
    CHECK(cerv_http_request_line_parse(span("GET  / HTTP/1.1"), &r) == CERV_PARSE_BAD_REQUEST);
    {
        size_t huge_len = 1024U * 1024U;
        unsigned char *huge = malloc(huge_len);
        CHECK(huge != NULL);
        if (huge != NULL) {
            memcpy(huge, "GET /", 5U);
            memset(huge + 5U, 'a', huge_len - 5U);
            CHECK(cerv_http_request_line_parse((struct cerv_span){.ptr = huge, .len = huge_len}, &r) ==
                  CERV_PARSE_URI_TOO_LONG);
            free(huge);
        }
    }
    CHECK(parse_text("GET / HTTP/1.1\r\nHost: example.com\r\n\r\n", &r) == CERV_PARSE_OK);
    CHECK(r.method == CERV_METHOD_GET && r.version_minor == 1U && r.field_count == 1U);
    CHECK(parse_text("HEAD /a HTTP/1.0\r\n\r\n", &r) == CERV_PARSE_OK);
    CHECK(r.connection_close);
    CHECK(parse_text("GET / HTTP/1.1\r\nHost: a\r\n\r\n", &r) == CERV_PARSE_OK && !r.connection_close);
    CHECK(parse_text("GET / HTTP/1.1\r\nHost: a\r\nConnection: close\r\n\r\n", &r) == CERV_PARSE_OK && r.connection_close);
    CHECK(parse_text("GET / HTTP/1.1\r\nHost: a\r\nConnection: keep-alive, close\r\n\r\n", &r) == CERV_PARSE_OK && r.connection_close);
    CHECK(parse_text("GET / HTTP/1.1\r\nHost: a\r\nConnection: , keep-alive,,\r\nConnection: upgrade\r\n\r\n", &r) == CERV_PARSE_OK && !r.connection_close);
    CHECK(parse_text("GET / HTTP/1.1\r\nHost: a\r\nConnection: keep alive\r\n\r\n", &r) == CERV_PARSE_BAD_REQUEST);
    CHECK(parse_text("GET / HTTP/1.1\r\n\r\n", &r) == CERV_PARSE_BAD_REQUEST);
    CHECK(parse_text("GET / HTTP/1.1\r\nHost: a\r\nHost: b\r\n\r\n", &r) == CERV_PARSE_BAD_REQUEST);
    CHECK(parse_text("GET / HTTP/1.1\r\nHost : a\r\n\r\n", &r) == CERV_PARSE_BAD_REQUEST);
    CHECK(parse_text("GET\t/ HTTP/1.1\r\nHost: a\r\n\r\n", &r) == CERV_PARSE_BAD_REQUEST);
    CHECK(parse_text("GET  / HTTP/1.1\r\nHost: a\r\n\r\n", &r) == CERV_PARSE_BAD_REQUEST);
    CHECK(parse_text("GET / HTTP/1.1\nHost: a\n\n", &r) == CERV_PARSE_BAD_REQUEST);
    CHECK(parse_text("GET / HTTP/1.1\r\n Host: a\r\n\r\n", &r) == CERV_PARSE_BAD_REQUEST);
    CHECK(parse_text("GET / HTTP/1.1\r\nHost: a\r\n X: y\r\n\r\n", &r) == CERV_PARSE_BAD_REQUEST);
    CHECK(parse_text("GET / HTTP/1.1\r\nHost: a\r\nTransfer-Encoding: chunked\r\n\r\n", &r) == CERV_PARSE_BAD_REQUEST);
    CHECK(parse_text("GET / HTTP/1.1\r\nHost: a\r\nContent-Length: 0\r\n\r\n", &r) == CERV_PARSE_OK);
    CHECK(parse_text("GET / HTTP/1.1\r\nHost: a\r\nContent-Length: 1\r\n\r\n", &r) == CERV_PARSE_BAD_REQUEST);
    CHECK(parse_text("GET / HTTP/1.1\r\nHost: a\r\nContent-Length: 0\r\nContent-Length: 0\r\n\r\n", &r) == CERV_PARSE_BAD_REQUEST);
    CHECK(parse_text("GET / HTTP/1.1\r\nHost: a\r\nTransfer-Encoding: chunked\r\nContent-Length: 0\r\n\r\n", &r) == CERV_PARSE_BAD_REQUEST);
    CHECK(parse_text("GET / HTTP/1.1\r\nHost: a\r\n\r\nX", &r) == CERV_PARSE_BAD_REQUEST);
    CHECK(parse_text("GET / HTTP/1.1\r\nHost: a\r\nExpect: nope\r\n\r\n", &r) == CERV_PARSE_EXPECTATION_FAILED);
    CHECK(parse_text("GET / HTTP/1.1\r\nExpect: nope\r\n\r\n", &r) == CERV_PARSE_BAD_REQUEST);
    CHECK(parse_text("GET / HTTP/1.1\r\nHost: a\r\nExpect: 100-continue\r\n\r\n", &r) == CERV_PARSE_OK);
    CHECK(parse_text("GET / HTTP/1.1\r\nHost: a\r\nExpect: 100-CONTINUE\r\n\r\n", &r) == CERV_PARSE_OK);
    CHECK(parse_text("GET / HTTP/1.1\r\nHost: a\r\nExpect: , 100-continue,,\r\n\r\n", &r) == CERV_PARSE_OK);
    CHECK(parse_text("GET / HTTP/1.1\r\nHost: a\r\nExpect: ,,nope,\r\n\r\n", &r) == CERV_PARSE_EXPECTATION_FAILED);
    CHECK(parse_text("POST / HTTP/1.1\r\nHost: a\r\n\r\n", &r) == CERV_PARSE_METHOD_NOT_ALLOWED);
    CHECK(parse_text("FROB / HTTP/1.1\r\nHost: a\r\n\r\n", &r) == CERV_PARSE_NOT_IMPLEMENTED);
    CHECK(parse_text("post / HTTP/1.1\r\nHost: a\r\n\r\n", &r) == CERV_PARSE_NOT_IMPLEMENTED);
    CHECK(parse_text("GET / HTTP/2.0\r\nHost: a\r\n\r\n", &r) == CERV_PARSE_HTTP_VERSION_UNSUPPORTED);
    CHECK(parse_text("GET / HTTP/1.9\r\nHost: a\r\n\r\n", &r) == CERV_PARSE_OK);
    CHECK(parse_text("GET * HTTP/1.1\r\nHost: a\r\n\r\n", &r) == CERV_PARSE_BAD_REQUEST);
    CHECK(parse_text("OPTIONS * HTTP/1.1\r\nHost: a\r\n\r\n", &r) == CERV_PARSE_METHOD_NOT_ALLOWED);
    CHECK(parse_text("CONNECT example.com:443 HTTP/1.1\r\nHost: a\r\n\r\n", &r) == CERV_PARSE_METHOD_NOT_ALLOWED);
    CHECK(parse_text("GET http://target.example/a HTTP/1.1\r\nHost: other.example\r\n\r\n", &r) == CERV_PARSE_OK);
    CHECK(cerv_span_equal_ascii_ci(r.effective_authority, "target.example"));
    CHECK(parse_text("GET http://target.example/a HTTP/1.1\r\nHost: [:::]\r\n\r\n", &r) == CERV_PARSE_BAD_REQUEST);
    CHECK(parse_text("GET / HTTP/1.1\r\nHost: [:::]\r\n\r\n", &r) == CERV_PARSE_BAD_REQUEST);
    CHECK(parse_text("GET / HTTP/1.1\r\nHost: a\r\nRange: bytes=5-4\r\n\r\n", &r) == CERV_PARSE_BAD_REQUEST);
    CHECK(parse_text("GET / HTTP/1.1\r\nHost: a\r\nRange: bytes=0-1,3-4\r\n\r\n", &r) == CERV_PARSE_OK && r.range_result == CERV_RANGE_MULTIPLE);
    CHECK(parse_text("GET / HTTP/1.1\r\nHost: a\r\nRange: bytes=0-1\r\nRange: bytes=2-3\r\n\r\n", &r) == CERV_PARSE_BAD_REQUEST);
    CHECK(parse_text("GET / HTTP/1.1\r\nHost: a\r\nRange: items=abc\r\n\r\n", &r) == CERV_PARSE_OK &&
          r.range_result == CERV_RANGE_UNIT_UNSUPPORTED);
    CHECK(parse_text("GET / HTTP/1.1\r\nHost: a\r\nRange: items=\r\n\r\n", &r) == CERV_PARSE_BAD_REQUEST);
    CHECK(parse_text("GET / HTTP/1.1\r\nHost: a\r\nIf-Modified-Since: garbage\r\n\r\n", &r) == CERV_PARSE_OK);
    CHECK(parse_text("GET / HTTP/1.1\r\nHost: a\r\nAccept-Encoding: br;q=1, gzip;q=0.5\r\n\r\n", &r) == CERV_PARSE_OK &&
          cerv_accept_encoding_quality(&r.accept_encoding, "br") == 1000U);
    CHECK(parse_text("GET / HTTP/1.1\r\nHost: a\r\nAccept-Encoding: identity; q=0.5\r\n\r\n", &r) == CERV_PARSE_OK &&
          cerv_accept_encoding_quality(&r.accept_encoding, "identity") == 500U);
    CHECK(parse_text("GET / HTTP/1.1\r\nHost: a\r\nIf-None-Match: W/\"abc\", \"def\"\r\n\r\n", &r) == CERV_PARSE_OK);
    CHECK(parse_text("GET / HTTP/1.1\r\nHost: a\r\nAccept-Encoding: , gzip,, br,\r\nIf-None-Match: , W/\"abc\",,\r\nRange: bytes=,0-1,,\r\n\r\n", &r) == CERV_PARSE_OK &&
          r.range_result == CERV_RANGE_SINGLE && cerv_accept_encoding_quality(&r.accept_encoding, "br") == 1000U);
    CHECK(cerv_entity_tag_parse(span("\"abc\""), &current) == CERV_ETAG_OK);
    CHECK(cerv_http_request_if_none_match_matches(&r, current));
    CHECK(parse_text("GET / HTTP/1.1\r\nHost: a\r\nIf-None-Match: *\r\nIf-None-Match: \"abc\"\r\n\r\n", &r) == CERV_PARSE_BAD_REQUEST);
}

static char *make_request_with_target(size_t target_len, size_t *out_len)
{
    const char *prefix = "GET ";
    const char *suffix = " HTTP/1.1\r\nHost: a\r\n\r\n";
    size_t total = strlen(prefix) + target_len + strlen(suffix);
    char *p = malloc(total + 1U);
    if (p == NULL) return NULL;
    memcpy(p, prefix, strlen(prefix));
    p[strlen(prefix)] = '/';
    if (target_len > 1U) memset(p + strlen(prefix) + 1U, 'a', target_len - 1U);
    memcpy(p + strlen(prefix) + target_len, suffix, strlen(suffix));
    p[total] = '\0';
    *out_len = total;
    return p;
}

static char *make_request_field_count(size_t count, size_t *out_len)
{
    const char *start = "GET / HTTP/1.1\r\nHost: a\r\n";
    const char *field = "X: y\r\n";
    size_t extra = count > 0U ? count - 1U : 0U;
    size_t total = strlen(start) + extra * strlen(field) + 2U;
    char *p = malloc(total + 1U);
    size_t pos = 0U;
    if (p == NULL) return NULL;
    memcpy(p + pos, start, strlen(start)); pos += strlen(start);
    for (size_t i = 0U; i < extra; ++i) { memcpy(p + pos, field, strlen(field)); pos += strlen(field); }
    memcpy(p + pos, "\r\n", 2U); pos += 2U;
    p[pos] = '\0'; *out_len = pos; return p;
}

static char *make_request_field_line(size_t line_len, size_t *out_len)
{
    const char *start = "GET / HTTP/1.1\r\nHost: a\r\n";
    size_t total = strlen(start) + line_len + 2U + 2U;
    char *p = malloc(total + 1U);
    size_t pos = 0U;
    if (p == NULL || line_len < 2U) { free(p); return NULL; }
    memcpy(p + pos, start, strlen(start)); pos += strlen(start);
    p[pos++] = 'X'; p[pos++] = ':';
    memset(p + pos, 'a', line_len - 2U); pos += line_len - 2U;
    memcpy(p + pos, "\r\n\r\n", 4U); pos += 4U;
    p[pos] = '\0'; *out_len = pos; return p;
}

static char *make_total_header(size_t target_bytes)
{
    const char *start = "GET / HTTP/1.1\r\nHost: a\r\n";
    size_t base = strlen(start) + 2U;
    size_t remain = target_bytes - base;
    size_t line1_total = remain > 8194U ? 8194U : remain;
    size_t line2_total = remain - line1_total;
    char *p = malloc(target_bytes + 1U);
    size_t pos = 0U;
    if (p == NULL || target_bytes < base || line1_total < 4U || (line2_total != 0U && line2_total < 4U)) {
        free(p); return NULL;
    }
    memcpy(p + pos, start, strlen(start)); pos += strlen(start);
    p[pos++] = 'X'; p[pos++] = ':'; memset(p + pos, 'a', line1_total - 4U); pos += line1_total - 4U; p[pos++] = '\r'; p[pos++] = '\n';
    if (line2_total != 0U) {
        p[pos++] = 'Y'; p[pos++] = ':'; memset(p + pos, 'b', line2_total - 4U); pos += line2_total - 4U; p[pos++] = '\r'; p[pos++] = '\n';
    }
    p[pos++] = '\r'; p[pos++] = '\n';
    if (pos != target_bytes) { free(p); return NULL; }
    p[pos] = '\0'; return p;
}

static void test_request_boundaries(void)
{
    struct cerv_http_request r;
    size_t len = 0U;
    char *p = make_request_with_target(4083U, &len);
    CHECK(p != NULL && cerv_http_request_parse((const unsigned char *)p, len, &r) == CERV_PARSE_OK);
    free(p);
    p = make_request_with_target(4084U, &len);
    CHECK(p != NULL && cerv_http_request_parse((const unsigned char *)p, len, &r) == CERV_PARSE_URI_TOO_LONG);
    free(p);
    {
        unsigned char line[CERV_REQUEST_LINE_MAX + 1U];
        memcpy(line, "GET /", 5U);
        memset(line + 5U, 'a', sizeof(line) - 5U);
        CHECK(cerv_http_request_parse(line, sizeof(line), &r) == CERV_PARSE_URI_TOO_LONG);
    }

    p = make_request_field_line(CERV_FIELD_LINE_MAX - 1U, &len);
    CHECK(p != NULL && cerv_http_request_parse((const unsigned char *)p, len, &r) == CERV_PARSE_OK); free(p);
    p = make_request_field_line(CERV_FIELD_LINE_MAX, &len);
    CHECK(p != NULL && cerv_http_request_parse((const unsigned char *)p, len, &r) == CERV_PARSE_OK); free(p);
    p = make_request_field_line(CERV_FIELD_LINE_MAX + 1U, &len);
    CHECK(p != NULL && cerv_http_request_parse((const unsigned char *)p, len, &r) == CERV_PARSE_HEADERS_TOO_LARGE); free(p);
    {
        const char *prefix = "GET / HTTP/1.1\r\nHost: a\r\nX:";
        size_t prefix_len = strlen(prefix);
        size_t field_prefix_len = 2U;
        size_t total = prefix_len + (CERV_FIELD_LINE_MAX + 1U - field_prefix_len);
        unsigned char *raw = malloc(total);
        CHECK(raw != NULL);
        if (raw != NULL) {
            memcpy(raw, prefix, prefix_len);
            memset(raw + prefix_len, 'a', total - prefix_len);
            CHECK(cerv_http_request_parse(raw, total, &r) == CERV_PARSE_HEADERS_TOO_LARGE);
            free(raw);
        }
    }

    p = make_request_field_count(CERV_FIELD_COUNT_MAX - 1U, &len);
    CHECK(p != NULL && cerv_http_request_parse((const unsigned char *)p, len, &r) == CERV_PARSE_OK); free(p);
    p = make_request_field_count(CERV_FIELD_COUNT_MAX, &len);
    CHECK(p != NULL && cerv_http_request_parse((const unsigned char *)p, len, &r) == CERV_PARSE_OK); free(p);
    p = make_request_field_count(CERV_FIELD_COUNT_MAX + 1U, &len);
    CHECK(p != NULL && cerv_http_request_parse((const unsigned char *)p, len, &r) == CERV_PARSE_HEADERS_TOO_LARGE); free(p);

    p = make_total_header(CERV_REQUEST_BYTES_MAX - 1U);
    CHECK(p != NULL && cerv_http_request_parse((const unsigned char *)p, CERV_REQUEST_BYTES_MAX - 1U, &r) == CERV_PARSE_OK); free(p);
    p = make_total_header(CERV_REQUEST_BYTES_MAX);
    CHECK(p != NULL && cerv_http_request_parse((const unsigned char *)p, CERV_REQUEST_BYTES_MAX, &r) == CERV_PARSE_OK); free(p);
    p = make_total_header(CERV_REQUEST_BYTES_MAX + 1U);
    CHECK(p != NULL && cerv_http_request_parse((const unsigned char *)p, CERV_REQUEST_BYTES_MAX + 1U, &r) == CERV_PARSE_HEADERS_TOO_LARGE); free(p);
}

int main(void)
{
    test_checked_buffer_time();
    test_fields();
    test_parser_domain_bounds();
    test_targets();
    test_path_policy();
    test_dates();
    test_ranges();
    test_requests_basic();
    test_request_boundaries();
    if (tests_failed != 0U) {
        fprintf(stderr, "%u/%u tests failed\n", tests_failed, tests_run);
        return 1;
    }
    printf("ok: %u assertions\n", tests_run);
    return 0;
}
