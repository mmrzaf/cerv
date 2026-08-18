#define _GNU_SOURCE
#include "runtime/conn.h"
#include "linux/fs.h"
#include "fuzz_support.h"

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static struct cerv_fs_root fuzz_root = {.fd = -1};
static int fuzz_initialized;

static void fuzz_init_root(void)
{
    char dir[] = "/tmp/cerv-runtime-fuzz-XXXXXX";
    char path[256];
    int fd;
    if (fuzz_initialized) return;
    cerv_fuzz_require(mkdtemp(dir) != NULL);
    cerv_fuzz_require(snprintf(path, sizeof(path), "%s/fuzz.txt", dir) > 0);
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    cerv_fuzz_require(fd >= 0);
    cerv_fuzz_require(write(fd, "FUZZ-CONTENT", 12U) == 12);
    cerv_fuzz_require(close(fd) == 0);
    cerv_fuzz_require(cerv_fs_root_open(dir, &fuzz_root) == CERV_FS_ROOT_OK);
    fuzz_initialized = 1;
}

static struct cerv_duration fuzz_duration(uint64_t ms)
{
    struct cerv_duration duration = {0};
    cerv_fuzz_require(cerv_duration_from_ms(ms, &duration));
    return duration;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    struct cerv_conn slot;
    struct cerv_conn_arena arena;
    struct cerv_conn *conn = NULL;
    int sv[2] = {-1, -1};
    enum cerv_conn_result result;
    size_t sent = 0U;
    unsigned steps;
    unsigned char drain[8192];

    fuzz_init_root();
    cerv_fuzz_require(socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, sv) == 0);
    cerv_fuzz_require(cerv_conn_arena_init(&arena, &slot, 1U));
    cerv_fuzz_require(cerv_conn_arena_acquire(&arena, &conn) == CERV_SLOT_ACQUIRE_OK && conn != NULL);
    cerv_fuzz_require(cerv_conn_begin(conn, sv[0], (struct cerv_mono_time){.ns = UINT64_C(1000)},
                                      fuzz_duration(100U), fuzz_duration(1000U)));
    sv[0] = -1;

    while (sent < size) {
        ssize_t n = send(sv[1], data + sent, size - sent, MSG_NOSIGNAL | MSG_DONTWAIT);
        if (n > 0) sent += (size_t)n;
        else if (n < 0 && errno == EINTR) continue;
        else break;
    }

    result = cerv_conn_on_readable(conn, &fuzz_root, NULL, false, (struct cerv_mono_time){.ns = UINT64_C(2000)},
                                   INT64_C(1700000000), fuzz_duration(100U));
    cerv_fuzz_require(result != CERV_CONN_FATAL);
    for (steps = 0U; steps < 8U && result == CERV_CONN_KEEP; ++steps) {
        if (cerv_conn_wants_write(conn)) {
            result = cerv_conn_on_writable(conn, (struct cerv_mono_time){.ns = UINT64_C(3000) + steps},
                                           fuzz_duration(100U), fuzz_duration(100U));
            cerv_fuzz_require(result != CERV_CONN_FATAL);
            while (recv(sv[1], drain, sizeof(drain), MSG_DONTWAIT) > 0) { }
        } else if (cerv_conn_wants_read(conn)) {
            break;
        }
    }
    if (result == CERV_CONN_KEEP) {
        result = cerv_conn_on_deadline(conn, (struct cerv_mono_time){.ns = UINT64_MAX},
                                       INT64_C(1700000000), fuzz_duration(100U));
        cerv_fuzz_require(result == CERV_CONN_CLOSE || result == CERV_CONN_KEEP);
    }
    cerv_fuzz_require(conn->request_used <= CERV_REQUEST_BYTES_MAX);
    cerv_conn_cleanup(conn);
    cerv_fuzz_require(cerv_conn_arena_release(&arena, conn));
    cerv_fuzz_require(arena.active == 0U);
    cerv_fuzz_require(close(sv[1]) == 0);
    return 0;
}
