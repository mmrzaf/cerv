#define _GNU_SOURCE
#include "linux/fs.h"
#include "runtime/worker.h"

#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static unsigned checks;
static unsigned failures;

#define CHECK(expr) do { \
    ++checks; \
    if (!(expr)) { \
        ++failures; \
        fprintf(stderr, "FAIL %s:%d: %s (errno=%d)\n", __FILE__, __LINE__, #expr, errno); \
    } \
} while (0)

struct response_capture {
    unsigned char bytes[65536];
    size_t len;
    bool eof;
};

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
    struct sockaddr_in addr;
    socklen_t len = (socklen_t)sizeof(addr);
    fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, (socklen_t)sizeof(one));
    addr = (struct sockaddr_in){0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(0U);
    if (bind(fd, (const struct sockaddr *)&addr, (socklen_t)sizeof(addr)) != 0 || listen(fd, 256) != 0 ||
        getsockname(fd, (struct sockaddr *)&addr, &len) != 0) {
        (void)close(fd);
        return -1;
    }
    *port = ntohs(addr.sin_port);
    return fd;
}

static int connect_client(uint16_t port, int receive_buffer)
{
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    struct sockaddr_in addr;
    if (fd < 0) return -1;
    if (receive_buffer > 0) {
        (void)setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &receive_buffer, (socklen_t)sizeof(receive_buffer));
    }
    addr = (struct sockaddr_in){0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);
    if (connect(fd, (const struct sockaddr *)&addr, (socklen_t)sizeof(addr)) != 0) {
        (void)close(fd);
        return -1;
    }
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

static void capture_read(int fd, struct response_capture *capture)
{
    for (;;) {
        ssize_t n;
        if (capture->len == sizeof(capture->bytes)) return;
        n = recv(fd, capture->bytes + capture->len, sizeof(capture->bytes) - capture->len, MSG_DONTWAIT);
        if (n > 0) {
            capture->len += (size_t)n;
            continue;
        }
        if (n == 0) capture->eof = true;
        else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) capture->eof = true;
        return;
    }
}

static bool capture_contains(const struct response_capture *capture, const char *needle)
{
    size_t needle_len = strlen(needle);
    size_t i;
    if (needle_len > capture->len) return false;
    for (i = 0U; i + needle_len <= capture->len; ++i) {
        if (memcmp(capture->bytes + i, needle, needle_len) == 0) return true;
    }
    return false;
}

static size_t response_body_offset(const struct response_capture *capture)
{
    size_t i;
    for (i = 0U; i + 4U <= capture->len; ++i) {
        if (memcmp(capture->bytes + i, "\r\n\r\n", 4U) == 0) return i + 4U;
    }
    return SIZE_MAX;
}

static bool drive_response(struct cerv_worker *worker, int client, struct response_capture *capture, unsigned limit)
{
    unsigned i;
    for (i = 0U; i < limit && !capture->eof; ++i) {
        enum cerv_worker_result result = cerv_worker_run_once(worker, 10);
        if (result != CERV_WORKER_OK) return false;
        capture_read(client, capture);
    }
    return capture->eof;
}


static bool drive_active_to_zero(struct cerv_worker *worker, unsigned limit, int wait_ms)
{
    unsigned i;
    for (i = 0U; i < limit && cerv_worker_active_connections(worker) != 0U; ++i) {
        if (cerv_worker_run_once(worker, wait_ms) != CERV_WORKER_OK) return false;
    }
    return cerv_worker_active_connections(worker) == 0U;
}

static size_t count_open_fds(void)
{
    DIR *dir = opendir("/proc/self/fd");
    struct dirent *entry;
    size_t count = 0U;
    if (dir == NULL) return SIZE_MAX;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) ++count;
    }
    (void)closedir(dir);
    return count - 1U; /* Exclude the directory FD used for enumeration itself. */
}

static bool init_worker(struct cerv_worker *worker, int listener, const struct cerv_fs_root *root,
                        struct cerv_conn *slots, struct cerv_timer_node *timers, size_t capacity,
                        uint64_t header_ms, uint64_t write_ms, uint64_t lifetime_ms)
{
    struct cerv_worker_config config;
    if (!cerv_duration_from_ms(header_ms, &config.header_timeout) ||
        !cerv_duration_from_ms(write_ms, &config.write_timeout) ||
        !cerv_duration_from_ms(lifetime_ms, &config.max_lifetime)) return false;
    config.immutable = false;
    config.shared_listener_cooperative = false;
    return cerv_worker_init(worker, listener, root, slots, timers, capacity, config);
}

static void test_listener_contracts(const struct cerv_fs_root *root)
{
    uint16_t port = 0U;
    int listener = create_listener(&port);
    int flags;
    int fd_flags;
    int not_listening = -1;
    struct cerv_conn slot;
    struct cerv_timer_node timer;
    struct cerv_worker worker;
    struct cerv_worker_config config;
    (void)port;
    CHECK(listener >= 0);
    CHECK(cerv_worker_config_default(&config));
    if (listener < 0) return;
    flags = fcntl(listener, F_GETFL);
    fd_flags = fcntl(listener, F_GETFD);
    CHECK(flags >= 0 && fd_flags >= 0);
    if (flags >= 0) {
        CHECK(fcntl(listener, F_SETFL, flags & ~O_NONBLOCK) == 0);
        CHECK(!cerv_worker_init(&worker, listener, root, &slot, &timer, 1U, config));
        CHECK(fcntl(listener, F_SETFL, flags) == 0);
    }
    if (fd_flags >= 0) {
        CHECK(fcntl(listener, F_SETFD, fd_flags & ~FD_CLOEXEC) == 0);
        CHECK(!cerv_worker_init(&worker, listener, root, &slot, &timer, 1U, config));
        CHECK(fcntl(listener, F_SETFD, fd_flags) == 0);
    }
    not_listening = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    CHECK(not_listening >= 0);
    if (not_listening >= 0) {
        CHECK(!cerv_worker_init(&worker, not_listening, root, &slot, &timer, 1U, config));
        CHECK(close(not_listening) == 0);
    }
    CHECK(close(listener) == 0);
}

static void test_basic_requests(const struct cerv_fs_root *root)
{
    uint16_t port = 0U;
    int listener = create_listener(&port);
    struct cerv_conn slots[4];
    struct cerv_timer_node timers[4];
    struct cerv_worker worker;
    const char *requests[] = {
        "GET /hello.txt HTTP/1.1\r\nHost: example\r\nConnection: close\r\n\r\n",
        "HEAD /hello.txt HTTP/1.1\r\nHost: example\r\nConnection: close\r\n\r\n",
        "GET /hello.txt HTTP/1.1\r\nHost: example\r\nRange: bytes=1-3\r\nConnection: close\r\n\r\n",
        "GET /hello.txt HTTP/1.1\r\nBroken\r\n\r\n",
        "GET /hello.txt HTTP/1.1\r\nHost: example\r\n\r\nGET /hello.txt HTTP/1.1\r\nHost: example\r\nConnection: close\r\n\r\n"
    };
    const char *statuses[] = {"200 OK", "200 OK", "206 Partial Content", "400 Bad Request", "400 Bad Request"};
    size_t i;
    CHECK(listener >= 0);
    if (listener < 0) return;
    CHECK(init_worker(&worker, listener, root, slots, timers, 4U, 1000U, 1000U, 5000U));
    for (i = 0U; i < sizeof(requests) / sizeof(requests[0]); ++i) {
        int client = connect_client(port, 0);
        struct response_capture capture = {0};
        size_t body;
        CHECK(client >= 0);
        if (client < 0) continue;
        CHECK(send_all(client, requests[i], strlen(requests[i])));
        CHECK(drive_response(&worker, client, &capture, 200U));
        CHECK(capture_contains(&capture, statuses[i]));
        CHECK(capture_contains(&capture, "Connection: close\r\n"));
        body = response_body_offset(&capture);
        CHECK(body != SIZE_MAX);
        if (body != SIZE_MAX && i == 0U) {
            CHECK(capture.len - body == 5U && memcmp(capture.bytes + body, "HELLO", 5U) == 0);
        }
        if (body != SIZE_MAX && i == 1U) CHECK(capture.len == body);
        if (body != SIZE_MAX && i == 2U) {
            CHECK(capture.len - body == 3U && memcmp(capture.bytes + body, "ELL", 3U) == 0);
        }
        CHECK(close(client) == 0);
        CHECK(cerv_worker_active_connections(&worker) == 0U);
    }
    cerv_worker_destroy(&worker);
    CHECK(close(listener) == 0);
}

static void test_saturation(const struct cerv_fs_root *root)
{
    uint16_t port = 0U;
    int listener = create_listener(&port);
    struct cerv_conn slot;
    struct cerv_timer_node timer;
    struct cerv_worker worker;
    int clients[2] = {-1, -1};
    struct response_capture capture[2] = {0};
    unsigned overloaded = 0U;
    unsigned admitted = 0U;
    size_t i;
    CHECK(listener >= 0);
    if (listener < 0) return;
    CHECK(init_worker(&worker, listener, root, &slot, &timer, 1U, 1000U, 1000U, 5000U));
    clients[0] = connect_client(port, 0);
    clients[1] = connect_client(port, 0);
    CHECK(clients[0] >= 0 && clients[1] >= 0);
    CHECK(cerv_worker_run_once(&worker, 50) == CERV_WORKER_OK);
    for (i = 0U; i < 2U; ++i) {
        unsigned spin;
        for (spin = 0U; spin < 20U && !capture[i].eof; ++spin) capture_read(clients[i], &capture[i]);
        if (capture_contains(&capture[i], "503 Service Unavailable")) ++overloaded;
        else ++admitted;
    }
    CHECK(overloaded == 1U && admitted == 1U);
    CHECK(cerv_worker_active_connections(&worker) == 1U);
    for (i = 0U; i < 2U; ++i) {
        if (!capture_contains(&capture[i], "503 Service Unavailable")) CHECK(close(clients[i]) == 0);
        else CHECK(close(clients[i]) == 0);
    }
    CHECK(drive_active_to_zero(&worker, 20U, 10));
    cerv_worker_destroy(&worker);
    CHECK(close(listener) == 0);
}

static void test_accept_budget(const struct cerv_fs_root *root)
{
    enum { CLIENTS = 70 };
    uint16_t port = 0U;
    int listener = create_listener(&port);
    struct cerv_conn *slots = calloc(CLIENTS, sizeof(*slots));
    struct cerv_timer_node *timers = calloc(CLIENTS, sizeof(*timers));
    int clients[CLIENTS];
    struct cerv_worker worker;
    size_t i;
    CHECK(listener >= 0 && slots != NULL && timers != NULL);
    if (listener < 0 || slots == NULL || timers == NULL) goto out;
    CHECK(init_worker(&worker, listener, root, slots, timers, CLIENTS, 5000U, 1000U, 10000U));
    for (i = 0U; i < CLIENTS; ++i) {
        clients[i] = connect_client(port, 0);
        CHECK(clients[i] >= 0);
    }
    CHECK(cerv_worker_run_once(&worker, 50) == CERV_WORKER_OK);
    CHECK(cerv_worker_active_connections(&worker) == CERV_ACCEPT_QUANTUM);
    CHECK(cerv_worker_run_once(&worker, 50) == CERV_WORKER_OK);
    CHECK(cerv_worker_active_connections(&worker) == CLIENTS);
    for (i = 0U; i < CLIENTS; ++i) CHECK(close(clients[i]) == 0);
    CHECK(drive_active_to_zero(&worker, 100U, 10));
    cerv_worker_destroy(&worker);
out:
    free(timers);
    free(slots);
    if (listener >= 0) CHECK(close(listener) == 0);
}

static void test_deadlines(const struct cerv_fs_root *root)
{
    uint16_t port = 0U;
    int listener = create_listener(&port);
    struct cerv_conn slot;
    struct cerv_timer_node timer;
    struct cerv_worker worker;
    int client;
    struct response_capture capture = {0};
    CHECK(listener >= 0);
    if (listener < 0) return;
    CHECK(init_worker(&worker, listener, root, &slot, &timer, 1U, 20U, 100U, 1000U));
    client = connect_client(port, 0);
    CHECK(client >= 0);
    CHECK(cerv_worker_run_once(&worker, 50) == CERV_WORKER_OK); /* accept */
    CHECK(cerv_worker_run_once(&worker, 100) == CERV_WORKER_OK); /* timer -> 408 state */
    CHECK(drive_response(&worker, client, &capture, 50U));
    CHECK(capture_contains(&capture, "408 Request Timeout"));
    CHECK(close(client) == 0);
    cerv_worker_destroy(&worker);
    CHECK(close(listener) == 0);

    listener = create_listener(&port);
    CHECK(listener >= 0);
    if (listener < 0) return;
    CHECK(init_worker(&worker, listener, root, &slot, &timer, 1U, 500U, 500U, 20U));
    client = connect_client(port, 0);
    CHECK(client >= 0);
    CHECK(cerv_worker_run_once(&worker, 50) == CERV_WORKER_OK);
    CHECK(cerv_worker_run_once(&worker, 100) == CERV_WORKER_OK);
    capture = (struct response_capture){0};
    capture_read(client, &capture);
    CHECK(cerv_worker_active_connections(&worker) == 0U);
    CHECK(capture.eof && capture.len == 0U); /* hard lifetime closes without extending itself via an error response */
    CHECK(close(client) == 0);
    cerv_worker_destroy(&worker);
    CHECK(close(listener) == 0);
}

static void test_large_does_not_starve_small(const struct cerv_fs_root *root)
{
    uint16_t port = 0U;
    int listener = create_listener(&port);
    struct cerv_conn slots[2];
    struct cerv_timer_node timers[2];
    struct cerv_worker worker;
    int big_client;
    int small_client;
    struct response_capture small = {0};
    const char big_request[] = "GET /big.bin HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n";
    const char small_request[] = "GET /hello.txt HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n";
    unsigned i;
    CHECK(listener >= 0);
    if (listener < 0) return;
    CHECK(init_worker(&worker, listener, root, slots, timers, 2U, 1000U, 60U, 2000U));
    big_client = connect_client(port, 4096);
    small_client = connect_client(port, 0);
    CHECK(big_client >= 0 && small_client >= 0);
    CHECK(send_all(big_client, big_request, sizeof(big_request) - 1U));
    CHECK(send_all(small_client, small_request, sizeof(small_request) - 1U));
    {
        bool worker_ok = true;
        for (i = 0U; i < 100U && !small.eof; ++i) {
            if (cerv_worker_run_once(&worker, 10) != CERV_WORKER_OK) { worker_ok = false; break; }
            capture_read(small_client, &small);
        }
        CHECK(worker_ok && small.eof);
    }
    CHECK(capture_contains(&small, "200 OK"));
    CHECK(capture_contains(&small, "HELLO"));
    CHECK(close(small_client) == 0);
    /* The non-reading large client is eventually removed by write no-progress or lifetime. */
    CHECK(drive_active_to_zero(&worker, 100U, 20));
    CHECK(close(big_client) == 0);
    cerv_worker_destroy(&worker);
    CHECK(close(listener) == 0);
}

static void test_peer_reset_and_stop_accepting(const struct cerv_fs_root *root)
{
    uint16_t port = 0U;
    int listener = create_listener(&port);
    struct cerv_conn slot;
    struct cerv_timer_node timer;
    struct cerv_worker worker;
    int client;
    struct linger reset = {.l_onoff = 1, .l_linger = 0};
    struct sigaction pipe_action;
    int socket_flags;
    int fd_flags;
    CHECK(listener >= 0);
    if (listener < 0) return;
    CHECK(init_worker(&worker, listener, root, &slot, &timer, 1U, 1000U, 1000U, 5000U));
    CHECK(sigaction(SIGPIPE, NULL, &pipe_action) == 0 && pipe_action.sa_handler == SIG_IGN);
    client = connect_client(port, 0);
    CHECK(client >= 0);
    CHECK(cerv_worker_run_once(&worker, 50) == CERV_WORKER_OK);
    CHECK(cerv_worker_active_connections(&worker) == 1U);
    socket_flags = fcntl(slot.socket_fd, F_GETFL);
    fd_flags = fcntl(slot.socket_fd, F_GETFD);
    CHECK(socket_flags >= 0 && (socket_flags & O_NONBLOCK) != 0);
    CHECK(fd_flags >= 0 && (fd_flags & FD_CLOEXEC) != 0);
    CHECK(setsockopt(client, SOL_SOCKET, SO_LINGER, &reset, (socklen_t)sizeof(reset)) == 0);
    CHECK(close(client) == 0);
    CHECK(drive_active_to_zero(&worker, 20U, 10));
    CHECK(cerv_worker_stop_accepting(&worker));
    CHECK(cerv_worker_stop_accepting(&worker));
    CHECK(!worker.accepting);
    cerv_worker_destroy(&worker);
    CHECK(close(listener) == 0);
}

static void test_destroy_live_transfer(const struct cerv_fs_root *root)
{
    uint16_t port = 0U;
    int listener = create_listener(&port);
    struct cerv_conn slot;
    struct cerv_timer_node timer;
    struct cerv_worker worker;
    int client;
    struct response_capture capture = {0};
    const char request[] = "GET /hello.txt HTTP/1.1\r\nHost: destroy\r\nConnection: close\r\n\r\n";
    CHECK(listener >= 0);
    if (listener < 0) return;
    CHECK(init_worker(&worker, listener, root, &slot, &timer, 1U, 1000U, 1000U, 5000U));
    client = connect_client(port, 0);
    CHECK(client >= 0);
    if (client < 0) { cerv_worker_destroy(&worker); CHECK(close(listener) == 0); return; }
    CHECK(send_all(client, request, sizeof(request) - 1U));
    CHECK(cerv_worker_run_once(&worker, 50) == CERV_WORKER_OK); /* accept */
    CHECK(cerv_worker_run_once(&worker, 50) == CERV_WORKER_OK); /* parse + own representation */
    CHECK(cerv_worker_active_connections(&worker) == 1U);
    CHECK(slot.state == CERV_CONN_SEND_HEADERS);
    CHECK(slot.file_fd >= 0);
    cerv_worker_destroy(&worker);
    CHECK(cerv_worker_active_connections(&worker) == 0U);
    capture_read(client, &capture);
    CHECK(capture.eof);
    CHECK(close(client) == 0);
    CHECK(fcntl(listener, F_GETFD) >= 0); /* listener is borrowed, not destroyed */
    CHECK(close(listener) == 0);
}

static int child_accept_emfile(const struct cerv_fs_root *root)
{
    uint16_t port = 0U;
    int listener = create_listener(&port);
    struct cerv_conn slot;
    struct cerv_timer_node timer;
    struct cerv_worker worker;
    int client = -1;
    int fillers[128];
    size_t fill_count = 0U;
    struct rlimit original;
    struct rlimit limited;
    enum cerv_worker_result result = CERV_WORKER_FATAL;
    size_t i;
    if (listener < 0 || !init_worker(&worker, listener, root, &slot, &timer, 1U, 1000U, 1000U, 5000U)) return 10;
    client = connect_client(port, 0);
    if (client < 0) return 11;
    if (getrlimit(RLIMIT_NOFILE, &original) != 0) return 12;
    limited = original;
    if (limited.rlim_max < (rlim_t)32) return 13;
    limited.rlim_cur = limited.rlim_max < (rlim_t)64 ? limited.rlim_max : (rlim_t)64;
    if (setrlimit(RLIMIT_NOFILE, &limited) != 0) return 14;
    while (fill_count < sizeof(fillers) / sizeof(fillers[0])) {
        int fd = open("/dev/null", O_RDONLY | O_CLOEXEC);
        if (fd >= 0) { fillers[fill_count++] = fd; continue; }
        if (errno == EINTR) continue;
        if (errno == EMFILE) break;
        return 15;
    }
    if (fill_count == sizeof(fillers) / sizeof(fillers[0])) return 16;
    result = cerv_worker_run_once(&worker, 50);
    for (i = 0U; i < fill_count; ++i) (void)close(fillers[i]);
    (void)setrlimit(RLIMIT_NOFILE, &original);
    (void)close(client);
    cerv_worker_destroy(&worker);
    (void)close(listener);
    return result == CERV_WORKER_RESOURCE_EXHAUSTED ? 0 : 17;
}

static void test_accept_fd_exhaustion(const struct cerv_fs_root *root)
{
    pid_t pid = fork();
    int status = 0;
    CHECK(pid >= 0);
    if (pid < 0) return;
    if (pid == 0) _exit(child_accept_emfile(root));
    CHECK(waitpid(pid, &status, 0) == pid);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

static void test_sequential_stress(const struct cerv_fs_root *root)
{
    uint16_t port = 0U;
    int listener = create_listener(&port);
    struct cerv_conn slots[8];
    struct cerv_timer_node timers[8];
    struct cerv_worker worker;
    unsigned i;
    const char request[] = "GET /hello.txt HTTP/1.1\r\nHost: stress\r\nConnection: close\r\n\r\n";
    CHECK(listener >= 0);
    if (listener < 0) return;
    CHECK(init_worker(&worker, listener, root, slots, timers, 8U, 1000U, 1000U, 5000U));
    for (i = 0U; i < 300U; ++i) {
        int client = connect_client(port, 0);
        struct response_capture capture = {0};
        CHECK(client >= 0);
        if (client < 0) break;
        CHECK(send_all(client, request, sizeof(request) - 1U));
        CHECK(drive_response(&worker, client, &capture, 100U));
        CHECK(capture_contains(&capture, "200 OK"));
        CHECK(capture_contains(&capture, "HELLO"));
        CHECK(close(client) == 0);
    }
    CHECK(cerv_worker_active_connections(&worker) == 0U);
    cerv_worker_destroy(&worker);
    CHECK(close(listener) == 0);
}

int main(void)
{
    char root_dir[] = "/tmp/cerv-worker-root-XXXXXX";
    char path[512];
    struct cerv_fs_root root = {.fd = -1};
    size_t fds_before = count_open_fds();
    size_t fds_after;
    int big_fd;

    CHECK(fds_before != SIZE_MAX);
    CHECK(mkdtemp(root_dir) != NULL);
    CHECK(snprintf(path, sizeof(path), "%s/hello.txt", root_dir) > 0);
    CHECK(write_file(path, "HELLO", 5U));
    CHECK(snprintf(path, sizeof(path), "%s/big.bin", root_dir) > 0);
    big_fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    CHECK(big_fd >= 0);
    if (big_fd >= 0) {
        CHECK(ftruncate(big_fd, (off_t)(16U * 1024U * 1024U)) == 0);
        CHECK(close(big_fd) == 0);
    }
    CHECK(cerv_fs_root_open(root_dir, &root) == CERV_FS_ROOT_OK);

    test_listener_contracts(&root);
    test_basic_requests(&root);
    test_saturation(&root);
    test_accept_budget(&root);
    test_deadlines(&root);
    test_large_does_not_starve_small(&root);
    test_peer_reset_and_stop_accepting(&root);
    test_destroy_live_transfer(&root);
    test_accept_fd_exhaustion(&root);
    test_sequential_stress(&root);

    cerv_fs_root_close(&root);
    CHECK(snprintf(path, sizeof(path), "%s/hello.txt", root_dir) > 0 && unlink(path) == 0);
    CHECK(snprintf(path, sizeof(path), "%s/big.bin", root_dir) > 0 && unlink(path) == 0);
    CHECK(rmdir(root_dir) == 0);
    fds_after = count_open_fds();
    CHECK(fds_after == fds_before);

    if (failures != 0U) {
        fprintf(stderr, "worker integration: %u/%u checks failed\n", failures, checks);
        return EXIT_FAILURE;
    }
    printf("worker integration: %u checks passed\n", checks);
    return EXIT_SUCCESS;
}
