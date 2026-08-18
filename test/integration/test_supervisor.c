#define _GNU_SOURCE
#include "base/bounds.h"
#include "process/config.h"
#include "process/supervisor.h"

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
#include <sys/types.h>
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

static void sleep_ms(unsigned ms)
{
    struct timespec req = {.tv_sec = (time_t)(ms / 1000U), .tv_nsec = (long)((ms % 1000U) * 1000000U)};
    while (nanosleep(&req, &req) != 0 && errno == EINTR) {}
}

static bool write_file(const char *path, const char *text)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    size_t len = strlen(text);
    size_t done = 0U;
    if (fd < 0) return false;
    while (done < len) {
        ssize_t n = write(fd, text + done, len - done);
        if (n > 0) done += (size_t)n;
        else if (n < 0 && errno == EINTR) continue;
        else { (void)close(fd); return false; }
    }
    return close(fd) == 0;
}

static uint16_t choose_port(void)
{
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    struct sockaddr_in addr = {0};
    socklen_t len = (socklen_t)sizeof(addr);
    uint16_t port = 0U;
    if (fd < 0) return 0U;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(0U);
    if (bind(fd, (const struct sockaddr *)(const void *)&addr, (socklen_t)sizeof(addr)) == 0 &&
        getsockname(fd, (struct sockaddr *)(void *)&addr, &len) == 0) port = ntohs(addr.sin_port);
    (void)close(fd);
    return port;
}

static bool make_config(struct cerv_config *config, const char *root, uint16_t port,
                        size_t workers, size_t max_connections, uint64_t header_ms,
                        uint64_t write_ms, uint64_t lifetime_ms, uint64_t shutdown_ms)
{
    struct sockaddr_in addr = {0};
    if (config == NULL || root == NULL) return false;
    memset(config, 0, sizeof(*config));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);
    memcpy(&config->listen_addr, &addr, sizeof(addr));
    config->listen_addr_len = (socklen_t)sizeof(addr);
    config->workers = workers;
    config->max_connections = max_connections;
    config->root_path = root;
    return cerv_duration_from_ms(header_ms, &config->header_timeout) &&
           cerv_duration_from_ms(write_ms, &config->write_timeout) &&
           cerv_duration_from_ms(lifetime_ms, &config->max_lifetime) &&
           cerv_duration_from_ms(shutdown_ms, &config->shutdown_timeout);
}

static pid_t spawn_supervisor(const struct cerv_config *config)
{
    pid_t pid = fork();
    if (pid == (pid_t)0) exit(cerv_supervisor_run(config));
    return pid;
}

static int connect_client(uint16_t port)
{
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    struct sockaddr_in addr = {0};
    struct timeval tv = {.tv_sec = 0, .tv_usec = 300000};
    if (fd < 0) return -1;
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, (socklen_t)sizeof(tv));
    (void)setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, (socklen_t)sizeof(tv));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);
    if (connect(fd, (const struct sockaddr *)(const void *)&addr, (socklen_t)sizeof(addr)) != 0) {
        (void)close(fd);
        return -1;
    }
    return fd;
}

static bool send_all(int fd, const char *text)
{
    size_t len = strlen(text);
    size_t done = 0U;
    while (done < len) {
        ssize_t n = send(fd, text + done, len - done, MSG_NOSIGNAL);
        if (n > 0) done += (size_t)n;
        else if (n < 0 && errno == EINTR) continue;
        else return false;
    }
    return true;
}

static size_t read_response(int fd, unsigned char *buf, size_t cap)
{
    size_t len = 0U;
    while (len < cap) {
        ssize_t n = recv(fd, buf + len, cap - len, 0);
        if (n > 0) len += (size_t)n;
        else if (n == 0) break;
        else if (errno == EINTR) continue;
        else if (errno == EAGAIN || errno == EWOULDBLOCK) break;
        else break;
    }
    return len;
}

static bool contains_bytes(const unsigned char *buf, size_t len, const char *needle)
{
    size_t nlen = strlen(needle);
    size_t i;
    if (nlen > len) return false;
    for (i = 0U; i + nlen <= len; ++i) if (memcmp(buf + i, needle, nlen) == 0) return true;
    return false;
}

static bool request_ok(uint16_t port, const char *request)
{
    int fd = connect_client(port);
    unsigned char response[8192];
    size_t len;
    if (fd < 0) return false;
    if (!send_all(fd, request)) { (void)close(fd); return false; }
    len = read_response(fd, response, sizeof(response));
    (void)close(fd);
    return contains_bytes(response, len, "200 OK") && contains_bytes(response, len, "HELLO");
}

static bool wait_ready(uint16_t port)
{
    unsigned i;
    for (i = 0U; i < 200U; ++i) {
        if (request_ok(port, "GET /hello.txt HTTP/1.1\r\nHost: test\r\nConnection: close\r\n\r\n")) return true;
        sleep_ms(10U);
    }
    return false;
}

static size_t child_pids(pid_t parent, pid_t *out, size_t cap)
{
    DIR *proc = opendir("/proc");
    struct dirent *entry;
    size_t count = 0U;
    if (proc == NULL) return 0U;
    while ((entry = readdir(proc)) != NULL) {
        char *end = NULL;
        long value = strtol(entry->d_name, &end, 10);
        char path[128];
        FILE *file;
        char line[256];
        long ppid = -1L;
        if (end == entry->d_name || *end != '\0' || value <= 0L) continue;
        (void)snprintf(path, sizeof(path), "/proc/%ld/status", value);
        file = fopen(path, "r");
        if (file == NULL) continue;
        while (fgets(line, sizeof(line), file) != NULL) {
            if (sscanf(line, "PPid:%ld", &ppid) == 1) break;
        }
        (void)fclose(file);
        if (ppid == (long)parent && count < cap) out[count++] = (pid_t)value;
    }
    (void)closedir(proc);
    return count;
}

static bool wait_children(pid_t parent, size_t expected, pid_t *out)
{
    unsigned i;
    for (i = 0U; i < 200U; ++i) {
        size_t count = child_pids(parent, out, CERV_WORKERS_MAX);
        if (count == expected) return true;
        sleep_ms(10U);
    }
    return false;
}

static size_t socket_fd_count(pid_t pid)
{
    char path[128];
    DIR *dir;
    struct dirent *entry;
    size_t count = 0U;
    (void)snprintf(path, sizeof(path), "/proc/%ld/fd", (long)pid);
    dir = opendir(path);
    if (dir == NULL) return 0U;
    while ((entry = readdir(dir)) != NULL) {
        char fd_path[512];
        char target[128];
        ssize_t n;
        if (entry->d_name[0] == '.') continue;
        (void)snprintf(fd_path, sizeof(fd_path), "%s/%s", path, entry->d_name);
        n = readlink(fd_path, target, sizeof(target) - 1U);
        if (n <= 0) continue;
        target[(size_t)n] = '\0';
        if (strncmp(target, "socket:[", 8U) == 0) ++count;
    }
    (void)closedir(dir);
    return count;
}

static bool wait_master(pid_t pid, unsigned timeout_ms, int *status)
{
    unsigned elapsed = 0U;
    while (elapsed <= timeout_ms) {
        pid_t got = waitpid(pid, status, WNOHANG);
        if (got == pid) return true;
        if (got < (pid_t)0 && errno != EINTR) return false;
        sleep_ms(10U);
        elapsed += 10U;
    }
    return false;
}

static bool workers_gone(const pid_t pids[], size_t count)
{
    size_t i;
    for (i = 0U; i < count; ++i) {
        if (pids[i] > (pid_t)0 && kill(pids[i], 0) == 0) return false;
        if (errno != ESRCH) return false;
    }
    return true;
}

static void test_clean_and_proxy(const char *root)
{
    struct cerv_config config;
    uint16_t port = choose_port();
    pid_t master;
    pid_t workers[4] = {0};
    int status = 0;
    CHECK(port != 0U);
    CHECK(make_config(&config, root, port, 3U, 12U, 1000U, 1000U, 5000U, 1000U));
    master = spawn_supervisor(&config);
    CHECK(master > (pid_t)0);
    if (master <= (pid_t)0) return;
    CHECK(wait_children(master, 3U, workers));
    CHECK(wait_ready(port));
    CHECK(request_ok(port, "GET http://example.test/hello.txt HTTP/1.1\r\nHost: proxy.internal\r\nX-Forwarded-For: 192.0.2.1\r\nX-Forwarded-Proto: https\r\n\r\n"));
    CHECK(kill(master, SIGINT) == 0);
    CHECK(wait_master(master, 3000U, &status));
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    sleep_ms(30U);
    CHECK(workers_gone(workers, 3U));
}

static void test_graceful_drain(const char *root)
{
    struct cerv_config config;
    uint16_t port = choose_port();
    pid_t master;
    int client = -1;
    unsigned char response[8192];
    size_t len = 0U;
    int status = 0;
    CHECK(make_config(&config, root, port, 2U, 4U, 2000U, 1000U, 5000U, 1500U));
    master = spawn_supervisor(&config);
    CHECK(master > (pid_t)0 && wait_ready(port));
    if (master <= (pid_t)0) return;
    client = connect_client(port);
    CHECK(client >= 0);
    if (client >= 0) {
        CHECK(send_all(client, "GET /hello.txt HTTP/1.1\r\nHost: test\r\n"));
        sleep_ms(50U);
        CHECK(kill(master, SIGTERM) == 0);
        sleep_ms(50U);
        CHECK(send_all(client, "\r\n"));
        len = read_response(client, response, sizeof(response));
        CHECK(contains_bytes(response, len, "200 OK"));
        CHECK(contains_bytes(response, len, "HELLO"));
        (void)close(client);
    }
    CHECK(wait_master(master, 3000U, &status));
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

static void test_hard_deadline_and_second_signal(const char *root)
{
    struct cerv_config config;
    uint16_t port = choose_port();
    pid_t master;
    int client;
    int status = 0;
    CHECK(make_config(&config, root, port, 2U, 4U, 10000U, 10000U, 20000U, 100U));
    master = spawn_supervisor(&config);
    CHECK(master > (pid_t)0 && wait_ready(port));
    client = connect_client(port);
    CHECK(client >= 0);
    if (client >= 0) CHECK(send_all(client, "G"));
    sleep_ms(30U);
    CHECK(kill(master, SIGTERM) == 0);
    CHECK(wait_master(master, 2000U, &status));
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    if (client >= 0) (void)close(client);

    port = choose_port();
    CHECK(make_config(&config, root, port, 2U, 4U, 10000U, 10000U, 20000U, 5000U));
    master = spawn_supervisor(&config);
    CHECK(master > (pid_t)0 && wait_ready(port));
    client = connect_client(port);
    CHECK(client >= 0);
    if (client >= 0) CHECK(send_all(client, "G"));
    sleep_ms(30U);
    CHECK(kill(master, SIGTERM) == 0);
    sleep_ms(30U);
    CHECK(kill(master, SIGTERM) == 0);
    CHECK(wait_master(master, 1500U, &status));
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    if (client >= 0) (void)close(client);
}

static void test_worker_crash(const char *root)
{
    struct cerv_config config;
    uint16_t port = choose_port();
    pid_t master;
    pid_t workers[4] = {0};
    int status = 0;
    CHECK(make_config(&config, root, port, 3U, 6U, 2000U, 1000U, 5000U, 500U));
    master = spawn_supervisor(&config);
    CHECK(master > (pid_t)0 && wait_children(master, 3U, workers) && wait_ready(port));
    if (master <= (pid_t)0 || workers[0] <= (pid_t)0) return;
    CHECK(kill(workers[0], SIGKILL) == 0);
    CHECK(wait_master(master, 3000U, &status));
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 1);
    sleep_ms(30U);
    CHECK(workers_gone(workers, 3U));
}

static void test_multiworker_saturation(const char *root)
{
    struct cerv_config config;
    uint16_t port = choose_port();
    pid_t master;
    pid_t workers[4] = {0};
    int clients[8];
    size_t i;
    bool filled = false;
    int status = 0;
    memset(clients, -1, sizeof(clients));
    CHECK(make_config(&config, root, port, 4U, 8U, 5000U, 1000U, 10000U, 1000U));
    master = spawn_supervisor(&config);
    CHECK(master > (pid_t)0 && wait_children(master, 4U, workers) && wait_ready(port));
    if (master <= (pid_t)0) return;
    for (i = 0U; i < 8U; ++i) {
        clients[i] = connect_client(port);
        CHECK(clients[i] >= 0);
        if (clients[i] >= 0) CHECK(send_all(clients[i], "G"));
    }
    for (i = 0U; i < 100U; ++i) {
        size_t w;
        size_t active = 0U;
        size_t used_workers = 0U;
        for (w = 0U; w < 4U; ++w) {
            size_t sockets = socket_fd_count(workers[w]);
            size_t clients_here = sockets > 0U ? sockets - 1U : 0U;
            active += clients_here;
            if (clients_here != 0U) ++used_workers;
        }
        if (active == 8U && used_workers == 4U) { filled = true; break; }
        sleep_ms(10U);
    }
    CHECK(filled);
    for (i = 0U; i < 8U; ++i) if (clients[i] >= 0) (void)close(clients[i]);
    sleep_ms(100U);
    CHECK(request_ok(port, "GET /hello.txt HTTP/1.1\r\nHost: test\r\nConnection: close\r\n\r\n"));
    CHECK(kill(master, SIGTERM) == 0);
    CHECK(wait_master(master, 3000U, &status));
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

static void test_startup_fd_refusal(const char *root)
{
    struct cerv_config config;
    uint16_t port = choose_port();
    pid_t pid;
    int status = 0;
    CHECK(make_config(&config, root, port, 1U, 100U, 1000U, 1000U, 5000U, 1000U));
    pid = fork();
    CHECK(pid >= (pid_t)0);
    if (pid == (pid_t)0) {
        struct rlimit limit = {.rlim_cur = 64U, .rlim_max = 64U};
        if (setrlimit(RLIMIT_NOFILE, &limit) != 0) _exit(90);
        exit(cerv_supervisor_run(&config));
    }
    if (pid > (pid_t)0) {
        CHECK(waitpid(pid, &status, 0) == pid);
        CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 1);
    }
}

int main(void)
{
    char root_template[] = "/tmp/cerv-supervisor-XXXXXX";
    char path[256];
    char *root = mkdtemp(root_template);
    CHECK(root != NULL);
    if (root == NULL) return 1;
    (void)snprintf(path, sizeof(path), "%s/hello.txt", root);
    CHECK(write_file(path, "HELLO"));
    test_clean_and_proxy(root);
    test_graceful_drain(root);
    test_hard_deadline_and_second_signal(root);
    test_worker_crash(root);
    test_multiworker_saturation(root);
    test_startup_fd_refusal(root);
    CHECK(unlink(path) == 0);
    CHECK(rmdir(root) == 0);
    if (failures != 0U) {
        fprintf(stderr, "supervisor integration: %u/%u checks failed\n", failures, checks);
        return 1;
    }
    printf("supervisor integration: %u checks passed\n", checks);
    return 0;
}
