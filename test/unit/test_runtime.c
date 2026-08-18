#define _POSIX_C_SOURCE 200809L
#include "runtime/conn.h"
#include "runtime/timer.h"
#include "runtime/worker.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

static unsigned checks;
static unsigned failures;

#define CHECK(expr) do { \
    ++checks; \
    if (!(expr)) { \
        ++failures; \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
    } \
} while (0)

static struct cerv_duration duration_ms(uint64_t ms)
{
    struct cerv_duration out = {0};
    CHECK(cerv_duration_from_ms(ms, &out));
    return out;
}

static bool bytes_contain(const unsigned char *buf, size_t len, const char *needle)
{
    size_t needle_len = strlen(needle);
    size_t i;
    if (needle_len > len) return false;
    for (i = 0U; i + needle_len <= len; ++i) {
        if (memcmp(buf + i, needle, needle_len) == 0) return true;
    }
    return false;
}

static void test_worker_defaults(void)
{
    struct cerv_worker_config config;
    struct cerv_duration expected;
    CHECK(cerv_worker_config_default(&config));
    CHECK(cerv_duration_from_ms(CERV_DEFAULT_HEADER_TIMEOUT_MS, &expected));
    CHECK(config.header_timeout.ns == expected.ns);
    CHECK(cerv_duration_from_ms(CERV_DEFAULT_WRITE_TIMEOUT_MS, &expected));
    CHECK(config.write_timeout.ns == expected.ns);
    CHECK(cerv_duration_from_ms(CERV_DEFAULT_MAX_LIFETIME_MS, &expected));
    CHECK(config.max_lifetime.ns == expected.ns);
    CHECK(!config.immutable);
    CHECK(!cerv_worker_config_default(NULL));
}

static void test_timer_heap(void)
{
    struct cerv_timer_node storage[4];
    struct cerv_timer_link links[4];
    struct cerv_timer_heap heap;
    struct cerv_timer_node node;
    size_t i;
    for (i = 0U; i < 4U; ++i) cerv_timer_link_init(&links[i]);
    CHECK(cerv_timer_heap_init(&heap, storage, 4U));
    CHECK(cerv_timer_heap_valid(&heap));
    CHECK(cerv_timer_set(&heap, &links[0], 0U, 1U, 50U));
    CHECK(cerv_timer_set(&heap, &links[1], 1U, 1U, 20U));
    CHECK(cerv_timer_set(&heap, &links[2], 2U, 1U, 30U));
    CHECK(cerv_timer_set(&heap, &links[3], 3U, 1U, 20U));
    CHECK(cerv_timer_heap_valid(&heap));
    CHECK(cerv_timer_peek(&heap, &node) && node.slot_index == 1U && node.deadline_ns == 20U);
    CHECK(cerv_timer_set(&heap, &links[0], 0U, 1U, 10U));
    CHECK(cerv_timer_peek(&heap, &node) && node.slot_index == 0U && node.deadline_ns == 10U);
    CHECK(cerv_timer_remove(&heap, &links[0]));
    CHECK(links[0].heap_index == CERV_TIMER_NOT_IN_HEAP);
    CHECK(cerv_timer_remove(&heap, &links[0]));
    CHECK(cerv_timer_heap_valid(&heap));
    CHECK(cerv_timer_pop(&heap, &node) && node.slot_index == 1U);
    CHECK(cerv_timer_pop(&heap, &node) && node.slot_index == 3U);
    CHECK(cerv_timer_pop(&heap, &node) && node.slot_index == 2U);
    CHECK(!cerv_timer_pop(&heap, &node));
    CHECK(cerv_timer_heap_valid(&heap));
}

static void test_arena_generation(void)
{
    struct cerv_conn slots[2];
    struct cerv_conn_arena arena;
    struct cerv_conn *a = NULL;
    struct cerv_conn *b = NULL;
    struct cerv_conn *c = NULL;
    uint64_t stale;
    CHECK(cerv_conn_arena_init(&arena, slots, 2U));
    CHECK(cerv_conn_arena_acquire(&arena, &a) == CERV_SLOT_ACQUIRE_OK && a != NULL);
    CHECK(cerv_conn_arena_acquire(&arena, &b) == CERV_SLOT_ACQUIRE_OK && b != NULL && b != a);
    CHECK(cerv_conn_arena_acquire(&arena, &c) == CERV_SLOT_ACQUIRE_FULL && c == NULL);
    stale = cerv_conn_token(a);
    CHECK(cerv_conn_arena_lookup_token(&arena, stale) == NULL); /* Slot is not active until begin. */
    CHECK(cerv_conn_begin(a, dup(STDIN_FILENO), (struct cerv_mono_time){.ns = 1U}, duration_ms(1U), duration_ms(2U)));
    CHECK(cerv_conn_arena_lookup_token(&arena, stale) == a);
    cerv_conn_cleanup(a);
    CHECK(cerv_conn_arena_release(&arena, a));
    CHECK(cerv_conn_arena_acquire(&arena, &c) == CERV_SLOT_ACQUIRE_OK && c == a);
    CHECK(cerv_conn_token(c) != stale);
    CHECK(cerv_conn_arena_lookup_token(&arena, stale) == NULL);
    CHECK(cerv_conn_arena_release(&arena, c));
    CHECK(cerv_conn_arena_release(&arena, b));
    CHECK(arena.active == 0U);
}

static void test_timeout_response(void)
{
    int sv[2] = {-1, -1};
    struct cerv_conn slots[1];
    struct cerv_conn_arena arena;
    struct cerv_conn *conn = NULL;
    struct cerv_duration header = duration_ms(10U);
    struct cerv_duration write = duration_ms(50U);
    struct cerv_duration lifetime = duration_ms(1000U);
    struct cerv_mono_time deadline;
    unsigned char wire[1024];
    ssize_t n;
    enum cerv_conn_result result;

    CHECK(socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, sv) == 0);
    CHECK(cerv_conn_arena_init(&arena, slots, 1U));
    CHECK(cerv_conn_arena_acquire(&arena, &conn) == CERV_SLOT_ACQUIRE_OK);
    CHECK(cerv_conn_begin(conn, sv[0], (struct cerv_mono_time){.ns = 100U}, header, lifetime));
    sv[0] = -1;
    CHECK(cerv_conn_next_deadline(conn, &deadline) && deadline.ns == 100U + header.ns);
    result = cerv_conn_on_deadline(conn, deadline, INT64_C(1700000000), write);
    CHECK(result == CERV_CONN_KEEP && conn->state == CERV_CONN_SEND_HEADERS);
    CHECK(conn->write_deadline.ns == deadline.ns + write.ns);
    result = cerv_conn_on_writable(conn, deadline, header, write);
    CHECK(result == CERV_CONN_CLOSE);
    n = recv(sv[1], wire, sizeof(wire), 0);
    CHECK(n > 0);
    if (n > 0) CHECK(bytes_contain(wire, (size_t)n, "HTTP/1.1 408 Request Timeout\r\n"));
    cerv_conn_cleanup(conn);
    CHECK(cerv_conn_arena_release(&arena, conn));
    CHECK(close(sv[1]) == 0);
}

static void test_lifetime_precedes_header_timeout(void)
{
    int sv[2] = {-1, -1};
    struct cerv_conn slot;
    struct cerv_conn_arena arena;
    struct cerv_conn *conn = NULL;
    struct cerv_duration header = duration_ms(100U);
    struct cerv_duration write = duration_ms(100U);
    struct cerv_duration lifetime = duration_ms(10U);
    struct cerv_mono_time deadline;
    CHECK(socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, sv) == 0);
    CHECK(cerv_conn_arena_init(&arena, &slot, 1U));
    CHECK(cerv_conn_arena_acquire(&arena, &conn) == CERV_SLOT_ACQUIRE_OK);
    CHECK(cerv_conn_begin(conn, sv[0], (struct cerv_mono_time){.ns = 0U}, header, lifetime));
    sv[0] = -1;
    CHECK(cerv_conn_next_deadline(conn, &deadline) && deadline.ns == lifetime.ns);
    CHECK(cerv_conn_on_deadline(conn, deadline, INT64_C(1700000000), write) == CERV_CONN_CLOSE);
    cerv_conn_cleanup(conn);
    CHECK(cerv_conn_arena_release(&arena, conn));
    CHECK(close(sv[1]) == 0);
}

static void test_fallback_transfer(void)
{
    char path[] = "/tmp/cerv-runtime-unit-XXXXXX";
    int file_fd = mkstemp(path);
    int sv[2] = {-1, -1};
    struct cerv_conn conn = {0};
    unsigned char received[32];
    ssize_t n;
    enum cerv_conn_result result;
    const char payload[] = "fallback-transfer";
    CHECK(file_fd >= 0);
    if (file_fd < 0) return;
    CHECK(unlink(path) == 0);
    CHECK(write(file_fd, payload, sizeof(payload) - 1U) == (ssize_t)(sizeof(payload) - 1U));
    CHECK(socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, sv) == 0);
    conn.state = CERV_CONN_SEND_FALLBACK;
    conn.socket_fd = sv[0];
    conn.file_fd = file_fd;
    conn.file_offset = 0U;
    conn.file_remaining = sizeof(payload) - 1U;
    conn.write_deadline.ns = 1U;
    conn.close_after_response = true;
    result = cerv_conn_on_writable(&conn, (struct cerv_mono_time){.ns = 10U}, duration_ms(100U), duration_ms(100U));
    CHECK(result == CERV_CONN_CLOSE);
    CHECK(conn.file_remaining == 0U);
    CHECK(conn.write_deadline.ns > 10U);
    n = recv(sv[1], received, sizeof(received), 0);
    CHECK(n == (ssize_t)(sizeof(payload) - 1U));
    if (n > 0) CHECK(memcmp(received, payload, (size_t)n) == 0);
    cerv_conn_cleanup(&conn);
    sv[0] = -1;
    CHECK(close(sv[1]) == 0);
}

static void test_header_deadline_is_absolute(void)
{
    int sv[2] = {-1, -1};
    struct cerv_conn slot;
    struct cerv_conn_arena arena;
    struct cerv_conn *conn = NULL;
    struct cerv_fs_root unused_root = {.fd = -1};
    struct cerv_duration header = duration_ms(100U);
    struct cerv_duration write = duration_ms(50U);
    struct cerv_duration lifetime = duration_ms(1000U);
    struct cerv_mono_time original_deadline;
    struct cerv_mono_time next_deadline;
    enum cerv_conn_result result;
    const char partial[] = "GET /";

    CHECK(socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, sv) == 0);
    CHECK(cerv_conn_arena_init(&arena, &slot, 1U));
    CHECK(cerv_conn_arena_acquire(&arena, &conn) == CERV_SLOT_ACQUIRE_OK);
    CHECK(cerv_conn_begin(conn, sv[0], (struct cerv_mono_time){.ns = 1000U}, header, lifetime));
    sv[0] = -1;
    CHECK(cerv_conn_next_deadline(conn, &original_deadline));
    CHECK(send(sv[1], partial, sizeof(partial) - 1U, MSG_NOSIGNAL) == (ssize_t)(sizeof(partial) - 1U));
    result = cerv_conn_on_readable(conn, &unused_root, NULL, false,
                                   (struct cerv_mono_time){.ns = original_deadline.ns - 1U},
                                   INT64_C(1700000000), write);
    CHECK(result == CERV_CONN_KEEP && conn->state == CERV_CONN_RECV_HEADERS);
    CHECK(cerv_conn_next_deadline(conn, &next_deadline));
    CHECK(next_deadline.ns == original_deadline.ns);
    CHECK(cerv_conn_on_deadline(conn, original_deadline, INT64_C(1700000000), write) == CERV_CONN_KEEP);
    CHECK(conn->state == CERV_CONN_SEND_HEADERS);
    cerv_conn_cleanup(conn);
    CHECK(cerv_conn_arena_release(&arena, conn));
    CHECK(close(sv[1]) == 0);
}

static void test_write_deadline_requires_progress(void)
{
    int sv[2] = {-1, -1};
    struct cerv_conn conn = {0};
    struct cerv_duration write_timeout = duration_ms(1U);
    unsigned char junk[4096];
    unsigned char drain[8192];
    ssize_t n;
    bool full = false;
    enum cerv_conn_result result;
    memset(junk, 'J', sizeof(junk));

    CHECK(socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, sv) == 0);
    while (!full) {
        n = send(sv[0], junk, sizeof(junk), MSG_NOSIGNAL);
        if (n > 0) continue;
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) { full = true; break; }
        CHECK(false);
        break;
    }
    CHECK(full);
    conn.state = CERV_CONN_SEND_HEADERS;
    conn.socket_fd = sv[0];
    conn.file_fd = -1;
    conn.response.headers[0] = (unsigned char)'X';
    conn.response.header_len = 1U;
    conn.write_deadline.ns = UINT64_C(2000000);
    conn.lifetime_deadline.ns = UINT64_MAX;
    conn.close_after_response = true;
    result = cerv_conn_on_writable(&conn, (struct cerv_mono_time){.ns = UINT64_C(1000000)}, duration_ms(100U), write_timeout);
    CHECK(result == CERV_CONN_KEEP);
    CHECK(conn.header_sent == 0U);
    CHECK(conn.write_deadline.ns == UINT64_C(2000000));

    for (;;) {
        n = recv(sv[1], drain, sizeof(drain), 0);
        if (n > 0) continue;
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
        CHECK(false);
        break;
    }
    result = cerv_conn_on_writable(&conn, (struct cerv_mono_time){.ns = UINT64_C(1500000)}, duration_ms(100U), write_timeout);
    CHECK(result == CERV_CONN_CLOSE);
    CHECK(conn.header_sent == 1U);
    CHECK(conn.write_deadline.ns == UINT64_C(2500000));
    n = recv(sv[1], drain, sizeof(drain), 0);
    CHECK(n == 1 && drain[0] == (unsigned char)'X');
    cerv_conn_cleanup(&conn);
    sv[0] = -1;
    CHECK(close(sv[1]) == 0);
}

static void test_keepalive_close_policy(void)
{
    CHECK(!cerv_conn_should_close_response(false, true, UINT32_C(0)));
    CHECK(!cerv_conn_should_close_response(false, true, CERV_KEEPALIVE_REQUESTS_MAX - UINT32_C(2)));
    CHECK(cerv_conn_should_close_response(false, true, CERV_KEEPALIVE_REQUESTS_MAX - UINT32_C(1)));
    CHECK(cerv_conn_should_close_response(false, true, CERV_KEEPALIVE_REQUESTS_MAX));
    CHECK(cerv_conn_should_close_response(true, true, UINT32_C(0)));
    CHECK(cerv_conn_should_close_response(false, false, UINT32_C(0)));
}

static void test_keepalive_response_reset(void)
{
    int sv[2] = {-1, -1};
    struct cerv_conn conn = {0};
    struct cerv_duration header = duration_ms(25U);
    struct cerv_duration write = duration_ms(100U);
    struct cerv_mono_time now = {.ns = UINT64_C(1000000)};
    unsigned char byte = 0U;
    enum cerv_conn_result result;

    CHECK(socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, sv) == 0);
    conn.state = CERV_CONN_SEND_HEADERS;
    conn.socket_fd = sv[0];
    conn.file_fd = -1;
    conn.response.headers[0] = (unsigned char)'X';
    conn.response.header_len = 1U;
    conn.write_deadline.ns = UINT64_C(2000000);
    conn.lifetime_deadline.ns = UINT64_C(9000000);
    conn.close_after_response = false;
    result = cerv_conn_on_writable(&conn, now, header, write);
    CHECK(result == CERV_CONN_KEEP);
    CHECK(conn.state == CERV_CONN_RECV_HEADERS);
    CHECK(conn.requests_completed == UINT32_C(1));
    CHECK(conn.request_used == 0U && conn.header_delimiter_state == 0U);
    CHECK(conn.header_deadline.ns == now.ns + header.ns);
    CHECK(conn.write_deadline.ns == UINT64_MAX);
    CHECK(conn.lifetime_deadline.ns == UINT64_C(9000000));
    CHECK(recv(sv[1], &byte, 1U, 0) == 1 && byte == (unsigned char)'X');
    cerv_conn_cleanup(&conn);
    sv[0] = -1;
    CHECK(close(sv[1]) == 0);
}

static void test_sendfile_peer_close_cannot_sigpipe(void)
{
    char path[] = "/tmp/cerv-runtime-sigpipe-XXXXXX";
    int file_fd = mkstemp(path);
    int sv[2] = {-1, -1};
    struct cerv_conn conn = {0};
    enum cerv_conn_result result;
    const char payload[] = "sendfile-sigpipe";

    CHECK(file_fd >= 0);
    if (file_fd < 0) return;
    CHECK(unlink(path) == 0);
    CHECK(write(file_fd, payload, sizeof(payload) - 1U) == (ssize_t)(sizeof(payload) - 1U));
    CHECK(socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, sv) == 0);
    CHECK(cerv_worker_prepare_process());
    CHECK(close(sv[1]) == 0);
    sv[1] = -1;
    conn.state = CERV_CONN_SEND_FILE;
    conn.socket_fd = sv[0];
    conn.file_fd = file_fd;
    conn.file_offset = 0U;
    conn.file_remaining = sizeof(payload) - 1U;
    conn.write_deadline.ns = UINT64_C(1000000000);
    conn.lifetime_deadline.ns = UINT64_MAX;
    result = cerv_conn_on_writable(&conn, (struct cerv_mono_time){.ns = 1U}, duration_ms(100U), duration_ms(100U));
    CHECK(result == CERV_CONN_CLOSE);
    cerv_conn_cleanup(&conn);
    sv[0] = -1;
}

int main(void)
{
    test_worker_defaults();
    test_timer_heap();
    test_arena_generation();
    test_timeout_response();
    test_lifetime_precedes_header_timeout();
    test_fallback_transfer();
    test_header_deadline_is_absolute();
    test_write_deadline_requires_progress();
    test_keepalive_close_policy();
    test_keepalive_response_reset();
    test_sendfile_peer_close_cannot_sigpipe();
    if (failures != 0U) {
        fprintf(stderr, "runtime unit: %u/%u failed\n", failures, checks);
        return EXIT_FAILURE;
    }
    printf("runtime unit: %u checks passed\n", checks);
    return EXIT_SUCCESS;
}
