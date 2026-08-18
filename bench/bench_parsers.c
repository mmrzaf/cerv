#define _POSIX_C_SOURCE 200809L

#include "http/http_fields.h"
#include "http/http_range.h"
#include "http/http_request.h"
#include "http/http_target.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static uint64_t monotonic_ns(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return UINT64_C(0);
    if (ts.tv_sec < 0) return UINT64_C(0);
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

static void print_rate(const char *name, uint64_t elapsed, uint64_t iterations)
{
    double ns_per_op = iterations == UINT64_C(0) ? 0.0 : (double)elapsed / (double)iterations;
    printf("%-20s %10.2f ns/op  (%" PRIu64 " iterations)\n", name, ns_per_op, iterations);
}

int main(void)
{
    static const unsigned char request[] =
        "GET /assets/app.js?v=42 HTTP/1.1\r\n"
        "Host: example.com\r\n"
        "Accept-Encoding: br;q=1, gzip;q=0.8, identity;q=0.2\r\n"
        "If-None-Match: W/\"abc\", \"def\"\r\n"
        "Range: bytes=0-16383\r\n\r\n";
    static const unsigned char target_bytes[] = "/assets/%E2%82%AC/app.js?v=42";
    static const unsigned char ae_bytes[] = "br;q=1, gzip;q=0.8, identity;q=0.2";
    static const unsigned char range_bytes[] = "bytes=0-16383";
    const uint64_t iterations = UINT64_C(500000);
    volatile unsigned sink = 0U;
    uint64_t start;
    uint64_t end;

    start = monotonic_ns();
    for (uint64_t i = UINT64_C(0); i < iterations; ++i) {
        struct cerv_http_request parsed;
        sink ^= (unsigned)cerv_http_request_parse(request, sizeof(request) - 1U, &parsed);
    }
    end = monotonic_ns();
    print_rate("complete request", end - start, iterations);

    start = monotonic_ns();
    for (uint64_t i = UINT64_C(0); i < iterations; ++i) {
        struct cerv_http_target target;
        struct cerv_path path;
        enum cerv_target_result r = cerv_http_target_parse(
            (struct cerv_span){.ptr = target_bytes, .len = sizeof(target_bytes) - 1U}, &target);
        if (r == CERV_TARGET_OK) r = cerv_http_target_decode_path(&target, &path);
        sink ^= (unsigned)r;
    }
    end = monotonic_ns();
    print_rate("target + decode", end - start, iterations);

    start = monotonic_ns();
    for (uint64_t i = UINT64_C(0); i < iterations; ++i) {
        struct cerv_accept_encoding ae;
        cerv_accept_encoding_init(&ae);
        sink ^= cerv_accept_encoding_add_field(
            &ae, (struct cerv_span){.ptr = ae_bytes, .len = sizeof(ae_bytes) - 1U}) ? 1U : 0U;
        sink ^= (unsigned)cerv_accept_encoding_quality(&ae, "br");
    }
    end = monotonic_ns();
    print_rate("Accept-Encoding", end - start, iterations);

    start = monotonic_ns();
    for (uint64_t i = UINT64_C(0); i < iterations; ++i) {
        struct cerv_range_spec range;
        struct cerv_range_selection selected;
        enum cerv_range_parse_result r = cerv_http_range_parse(
            (struct cerv_span){.ptr = range_bytes, .len = sizeof(range_bytes) - 1U}, &range);
        if (r == CERV_RANGE_SINGLE) {
            sink ^= (unsigned)cerv_http_range_normalize(range, UINT64_C(1048576), &selected);
        }
    }
    end = monotonic_ns();
    print_rate("Range parse+norm", end - start, iterations);

    if (sink == 0xffffffffU) printf("sink=%u\n", sink);
    return 0;
}
