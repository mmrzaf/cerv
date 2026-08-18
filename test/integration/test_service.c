#define _GNU_SOURCE
#include "base/bounds.h"
#include "linux/fs.h"
#include "runtime/worker.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
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
#define CHECK(expr) do { ++checks; if (!(expr)) { ++failures; fprintf(stderr, "FAIL %s:%d: %s (errno=%d)\n", __FILE__, __LINE__, #expr, errno); } } while (0)

static bool write_file(const char *path, const void *bytes, size_t len)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    size_t done = 0U;
    if (fd < 0) return false;
    while (done < len) {
        ssize_t n = write(fd, (const unsigned char *)bytes + done, len - done);
        if (n > 0) done += (size_t)n;
        else if (n < 0 && errno == EINTR) continue;
        else { (void)close(fd); return false; }
    }
    return close(fd) == 0;
}

static int create_listener(uint16_t *port)
{
    int fd;
    int one = 1;
    struct sockaddr_in addr = {0};
    socklen_t len = (socklen_t)sizeof(addr);
    fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, (socklen_t)sizeof(one)) != 0) { (void)close(fd); return -1; }
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(0U);
    if (bind(fd, (const struct sockaddr *)(const void *)&addr, (socklen_t)sizeof(addr)) != 0 ||
        listen(fd, 64) != 0 || getsockname(fd, (struct sockaddr *)(void *)&addr, &len) != 0) {
        (void)close(fd); return -1;
    }
    *port = ntohs(addr.sin_port);
    return fd;
}

static int connect_client(uint16_t port)
{
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    struct sockaddr_in addr = {0};
    if (fd < 0) return -1;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);
    if (connect(fd, (const struct sockaddr *)(const void *)&addr, (socklen_t)sizeof(addr)) != 0) { (void)close(fd); return -1; }
    return fd;
}

static bool send_all(int fd, const void *bytes, size_t len)
{
    size_t done = 0U;
    while (done < len) {
        ssize_t n = send(fd, (const unsigned char *)bytes + done, len - done, MSG_NOSIGNAL);
        if (n > 0) done += (size_t)n;
        else if (n < 0 && errno == EINTR) continue;
        else return false;
    }
    return true;
}

static size_t find_header_end(const unsigned char *buf, size_t len)
{
    size_t i;
    for (i = 0U; i + 4U <= len; ++i) if (memcmp(buf + i, "\r\n\r\n", 4U) == 0) return i + 4U;
    return SIZE_MAX;
}

static bool parse_content_length(const unsigned char *buf, size_t header_end, size_t *out)
{
    static const char key[] = "Content-Length: ";
    size_t i;
    if (out == NULL) return false;
    for (i = 0U; i + sizeof(key) - 1U < header_end; ++i) {
        if (memcmp(buf + i, key, sizeof(key) - 1U) == 0) {
            size_t p = i + sizeof(key) - 1U;
            size_t value = 0U;
            bool any = false;
            while (p < header_end && buf[p] >= (unsigned char)'0' && buf[p] <= (unsigned char)'9') {
                any = true;
                value = value * 10U + (size_t)(buf[p] - (unsigned char)'0');
                ++p;
            }
            if (!any) return false;
            *out = value;
            return true;
        }
    }
    return false;
}

static bool contains(const unsigned char *buf, size_t len, const char *needle)
{
    size_t n = strlen(needle), i;
    for (i = 0U; n <= len && i + n <= len; ++i) if (memcmp(buf + i, needle, n) == 0) return true;
    return false;
}

static bool drive_one_response(struct cerv_worker *worker, int client, unsigned char *buf, size_t cap, size_t *used)
{
    unsigned step;
    size_t header_end = SIZE_MAX;
    size_t content_length = 0U;
    *used = 0U;
    for (step = 0U; step < 500U; ++step) {
        ssize_t n;
        if (cerv_worker_run_once(worker, 10) != CERV_WORKER_OK) return false;
        do {
            n = recv(client, buf + *used, cap - *used, MSG_DONTWAIT);
            if (n > 0) *used += (size_t)n;
        } while (n > 0 && *used < cap);
        if (header_end == SIZE_MAX) {
            header_end = find_header_end(buf, *used);
            if (header_end != SIZE_MAX && !parse_content_length(buf, header_end, &content_length)) return false;
        }
        if (header_end != SIZE_MAX && *used == header_end + content_length) return true;
        if (*used == cap) return false;
        if (n == 0) return header_end != SIZE_MAX && *used == header_end + content_length;
        if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) return false;
    }
    return false;
}

static void test_keepalive_bound(const struct cerv_fs_root *root)
{
    uint16_t port = 0U;
    int listener = create_listener(&port);
    struct cerv_conn slot;
    struct cerv_timer_node timer;
    struct cerv_worker worker;
    struct cerv_worker_config config;
    int client;
    uint32_t i;
    const char request[] = "GET /hello.txt HTTP/1.1\r\nHost: keepalive\r\n\r\n";
    unsigned char response[4096];
    size_t used = 0U;

    CHECK(listener >= 0);
    CHECK(cerv_worker_config_default(&config));
    config.shared_listener_cooperative = false;
    CHECK(cerv_worker_init(&worker, listener, root, &slot, &timer, 1U, config));
    client = connect_client(port);
    CHECK(client >= 0);
    if (client < 0) { cerv_worker_destroy(&worker); (void)close(listener); return; }

    for (i = 0U; i < CERV_KEEPALIVE_REQUESTS_MAX; ++i) {
        CHECK(send_all(client, request, sizeof(request) - 1U));
        CHECK(drive_one_response(&worker, client, response, sizeof(response), &used));
        CHECK(contains(response, used, "HTTP/1.1 200 OK\r\n"));
        CHECK(contains(response, used, "HELLO"));
        if (i + UINT32_C(1) < CERV_KEEPALIVE_REQUESTS_MAX) {
            CHECK(!contains(response, used, "Connection: close\r\n"));
            CHECK(cerv_worker_active_connections(&worker) == 1U);
        } else {
            CHECK(contains(response, used, "Connection: close\r\n"));
        }
    }
    for (i = 0U; i < 20U && cerv_worker_active_connections(&worker) != 0U; ++i) {
        CHECK(cerv_worker_run_once(&worker, 1) == CERV_WORKER_OK);
    }
    CHECK(cerv_worker_active_connections(&worker) == 0U);
    CHECK(close(client) == 0);
    cerv_worker_destroy(&worker);
    CHECK(close(listener) == 0);
}


static void test_drain_retires_keepalive(const struct cerv_fs_root *root)
{
    uint16_t port = 0U;
    int listener = create_listener(&port);
    struct cerv_conn slot;
    struct cerv_timer_node timer;
    struct cerv_worker worker;
    struct cerv_worker_config config;
    int client;
    const char request[] = "GET /hello.txt HTTP/1.1\r\nHost: drain\r\n\r\n";
    unsigned char response[4096];
    size_t used = 0U;

    CHECK(listener >= 0);
    CHECK(cerv_worker_config_default(&config));
    config.shared_listener_cooperative = false;
    CHECK(cerv_worker_init(&worker, listener, root, &slot, &timer, 1U, config));
    client = connect_client(port);
    CHECK(client >= 0);
    CHECK(send_all(client, request, sizeof(request) - 1U));
    CHECK(drive_one_response(&worker, client, response, sizeof(response), &used));
    CHECK(!contains(response, used, "Connection: close\r\n"));
    CHECK(cerv_worker_active_connections(&worker) == 1U);

    CHECK(cerv_worker_stop_accepting(&worker));
    CHECK(send_all(client, request, sizeof(request) - 1U));
    CHECK(drive_one_response(&worker, client, response, sizeof(response), &used));
    CHECK(contains(response, used, "Connection: close\r\n"));
    for (unsigned i = 0U; i < 20U && cerv_worker_active_connections(&worker) != 0U; ++i) {
        CHECK(cerv_worker_run_once(&worker, 1) == CERV_WORKER_OK);
    }
    CHECK(cerv_worker_active_connections(&worker) == 0U);
    CHECK(close(client) == 0);
    cerv_worker_destroy(&worker);
    CHECK(close(listener) == 0);
}

static void test_client_close_request(const struct cerv_fs_root *root)
{
    uint16_t port = 0U;
    int listener = create_listener(&port);
    struct cerv_conn slot;
    struct cerv_timer_node timer;
    struct cerv_worker worker;
    struct cerv_worker_config config;
    int client;
    const char request[] = "GET /hello.txt HTTP/1.1\r\nHost: close\r\nConnection: close\r\n\r\n";
    unsigned char response[4096];
    size_t used = 0U;
    CHECK(listener >= 0);
    CHECK(cerv_worker_config_default(&config));
    config.shared_listener_cooperative = false;
    CHECK(cerv_worker_init(&worker, listener, root, &slot, &timer, 1U, config));
    client = connect_client(port);
    CHECK(client >= 0);
    CHECK(send_all(client, request, sizeof(request) - 1U));
    CHECK(drive_one_response(&worker, client, response, sizeof(response), &used));
    CHECK(contains(response, used, "Connection: close\r\n"));
    for (unsigned i = 0U; i < 20U && cerv_worker_active_connections(&worker) != 0U; ++i) {
        CHECK(cerv_worker_run_once(&worker, 1) == CERV_WORKER_OK);
    }
    CHECK(cerv_worker_active_connections(&worker) == 0U);
    CHECK(close(client) == 0);
    cerv_worker_destroy(&worker);
    CHECK(close(listener) == 0);
}

static void test_spa_fallback(const struct cerv_fs_root *root)
{
    uint16_t port = 0U;
    int listener = create_listener(&port);
    struct cerv_conn slot;
    struct cerv_timer_node timer;
    struct cerv_worker worker;
    struct cerv_worker_config config;
    int client;
    const char request[] = "GET /dashboard/settings HTTP/1.1\r\nHost: spa\r\nConnection: close\r\n\r\n";
    unsigned char response[4096];
    size_t used = 0U;
    static const char fallback[] = "index.html";
    CHECK(listener >= 0);
    CHECK(cerv_worker_config_default(&config));
    memcpy(config.spa_fallback.bytes, fallback, sizeof(fallback));
    config.spa_fallback.len = sizeof(fallback) - 1U;
    config.shared_listener_cooperative = false;
    CHECK(cerv_worker_init(&worker, listener, root, &slot, &timer, 1U, config));
    client = connect_client(port);
    CHECK(client >= 0);
    CHECK(send_all(client, request, sizeof(request) - 1U));
    CHECK(drive_one_response(&worker, client, response, sizeof(response), &used));
    CHECK(contains(response, used, "HTTP/1.1 200 OK\r\n"));
    CHECK(contains(response, used, "Content-Type: text/html; charset=utf-8\r\n"));
    CHECK(contains(response, used, "SPA_INDEX"));
    CHECK(contains(response, used, "Connection: close\r\n"));
    CHECK(close(client) == 0);
    cerv_worker_destroy(&worker);
    CHECK(close(listener) == 0);
}

int main(void)
{
    char root_dir[] = "/tmp/cerv-service-root-XXXXXX";
    char path[512];
    char index_path[512];
    struct cerv_fs_root root = {.fd = -1};
    CHECK(mkdtemp(root_dir) != NULL);
    CHECK(snprintf(path, sizeof(path), "%s/hello.txt", root_dir) > 0);
    CHECK(snprintf(index_path, sizeof(index_path), "%s/index.html", root_dir) > 0);
    CHECK(write_file(path, "HELLO", 5U));
    CHECK(write_file(index_path, "SPA_INDEX", 9U));
    CHECK(cerv_fs_root_open(root_dir, &root) == CERV_FS_ROOT_OK);
    test_keepalive_bound(&root);
    test_drain_retires_keepalive(&root);
    test_client_close_request(&root);
    test_spa_fallback(&root);
    cerv_fs_root_close(&root);
    CHECK(unlink(path) == 0);
    CHECK(unlink(index_path) == 0);
    CHECK(rmdir(root_dir) == 0);
    if (failures != 0U) { fprintf(stderr, "service integration: %u/%u failed\n", failures, checks); return EXIT_FAILURE; }
    printf("service integration: %u checks passed\n", checks);
    return EXIT_SUCCESS;
}
