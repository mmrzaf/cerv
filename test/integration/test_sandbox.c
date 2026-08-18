#define _GNU_SOURCE 1
#include "base/cerv_time.h"
#include "linux/fs.h"
#include "linux/sandbox.h"
#include "process/diag.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
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

static bool write_file(const char *path, const char *body)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    size_t len = strlen(body);
    size_t done = 0U;
    if (fd < 0) return false;
    while (done < len) {
        ssize_t n = write(fd, body + done, len - done);
        if (n > 0) done += (size_t)n;
        else if (n < 0 && errno == EINTR) continue;
        else { (void)close(fd); return false; }
    }
    return close(fd) == 0;
}

#ifndef CERV_INSTRUMENTED_BUILD
static int sandbox_allowed_child(const char *root_path)
{
    struct cerv_fs_root root = {.fd = -1};
    struct cerv_fs_file file = {.fd = -1};
    enum cerv_landlock_status landlock = CERV_LANDLOCK_UNAVAILABLE;
    struct timespec ts;
    if (cerv_fs_root_open(root_path, &root) != CERV_FS_ROOT_OK) return 10;
    if (!cerv_sandbox_worker_enter(&root, &landlock)) return 11;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 13;
    if (cerv_fs_open_regular(&root, "file.txt", &file) != CERV_FS_OK) return 14;
    cerv_fs_file_close(&file);
    cerv_fs_root_close(&root);
    return 0;
}

static void test_worker_seccomp_allowed(const char *root_path)
{
    pid_t pid = fork();
    int status = 0;
    CHECK(pid >= (pid_t)0);
    if (pid == (pid_t)0) _exit(sandbox_allowed_child(root_path));
    if (pid > (pid_t)0) {
        CHECK(waitpid(pid, &status, 0) == pid);
        CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }
}

static int sandbox_forbidden_child(const char *root_path)
{
    struct cerv_fs_root root = {.fd = -1};
    enum cerv_landlock_status landlock = CERV_LANDLOCK_UNAVAILABLE;
    if (cerv_fs_root_open(root_path, &root) != CERV_FS_ROOT_OK) return 20;
    if (!cerv_sandbox_worker_enter(&root, &landlock)) return 21;
    (void)syscall(SYS_getpid);
    return 22;
}

static void test_worker_seccomp_forbidden(const char *root_path)
{
    pid_t pid = fork();
    int status = 0;
    CHECK(pid >= (pid_t)0);
    if (pid == (pid_t)0) _exit(sandbox_forbidden_child(root_path));
    if (pid > (pid_t)0) {
        CHECK(waitpid(pid, &status, 0) == pid);
        CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGSYS);
    }
}

static int landlock_child(const char *root_path)
{
    struct cerv_fs_root root = {.fd = -1};
    struct cerv_fs_file inside = {.fd = -1};
    enum cerv_landlock_status status = CERV_LANDLOCK_UNAVAILABLE;
    int outside;
    if (cerv_fs_root_open(root_path, &root) != CERV_FS_ROOT_OK) return 30;
    if (!cerv_sandbox_no_new_privs()) return 31;
    if (prctl(PR_GET_NO_NEW_PRIVS, 0L, 0L, 0L, 0L) != 1) return 36;
    if (!cerv_sandbox_landlock_read_root(&root, &status)) return 32;
    if (cerv_fs_open_regular(&root, "file.txt", &inside) != CERV_FS_OK) return 33;
    cerv_fs_file_close(&inside);
    if (status == CERV_LANDLOCK_ENABLED) {
        outside = open("/etc/passwd", O_RDONLY | O_CLOEXEC);
        if (outside >= 0) { (void)close(outside); return 34; }
        if (errno != EACCES && errno != EPERM) return 35;
    }
    cerv_fs_root_close(&root);
    return status == CERV_LANDLOCK_ENABLED ? 0 : 100;
}

static void test_landlock(const char *root_path)
{
    pid_t pid = fork();
    int status = 0;
    CHECK(pid >= (pid_t)0);
    if (pid == (pid_t)0) _exit(landlock_child(root_path));
    if (pid > (pid_t)0) {
        CHECK(waitpid(pid, &status, 0) == pid);
        CHECK(WIFEXITED(status));
        if (WIFEXITED(status)) CHECK(WEXITSTATUS(status) == 0 || WEXITSTATUS(status) == 100);
    }
}

#endif

static void fill_nonblocking_fd(int fd)
{
    unsigned char bytes[1024];
    memset(bytes, 0x5a, sizeof(bytes));
    for (;;) {
        ssize_t n = write(fd, bytes, sizeof(bytes));
        if (n > 0) continue;
        if (n < 0 && errno == EINTR) continue;
        break;
    }
}

static uint64_t elapsed_ns(struct timespec before, struct timespec after)
{
    uint64_t sec;
    uint64_t nsec;
    if (after.tv_sec < before.tv_sec) return UINT64_MAX;
    sec = (uint64_t)(after.tv_sec - before.tv_sec);
    if (after.tv_nsec >= before.tv_nsec) return sec * UINT64_C(1000000000) + (uint64_t)(after.tv_nsec - before.tv_nsec);
    if (sec == UINT64_C(0)) return UINT64_MAX;
    nsec = UINT64_C(1000000000) - (uint64_t)before.tv_nsec + (uint64_t)after.tv_nsec;
    return (sec - UINT64_C(1)) * UINT64_C(1000000000) + nsec;
}

static void test_diag_full_pipe(void)
{
    int p[2] = {-1, -1};
    int saved = -1;
    struct timespec before;
    struct timespec after;
    CHECK(pipe2(p, O_NONBLOCK | O_CLOEXEC) == 0);
    if (p[0] < 0) return;
    fill_nonblocking_fd(p[1]);
    saved = dup(STDERR_FILENO);
    CHECK(saved >= 0);
    if (saved >= 0) {
        CHECK(dup2(p[1], STDERR_FILENO) == STDERR_FILENO);
        cerv_diag_close();
        cerv_diag_prepare();
        CHECK(clock_gettime(CLOCK_MONOTONIC, &before) == 0);
        cerv_diag_message("info", "pipe_full", "drop");
        CHECK(clock_gettime(CLOCK_MONOTONIC, &after) == 0);
        CHECK(elapsed_ns(before, after) < UINT64_C(50000000));
        cerv_diag_close();
        CHECK(dup2(saved, STDERR_FILENO) == STDERR_FILENO);
        cerv_diag_prepare();
        CHECK(close(saved) == 0);
    }
    CHECK(close(p[0]) == 0);
    CHECK(close(p[1]) == 0);
}

static void test_diag_full_socket(void)
{
    int sv[2] = {-1, -1};
    int saved = -1;
    int flags;
    struct timespec before;
    struct timespec after;
    CHECK(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sv) == 0);
    if (sv[0] < 0) return;
    flags = fcntl(sv[0], F_GETFL);
    CHECK(flags >= 0 && fcntl(sv[0], F_SETFL, flags | O_NONBLOCK) == 0);
    fill_nonblocking_fd(sv[0]);
    saved = dup(STDERR_FILENO);
    CHECK(saved >= 0);
    if (saved >= 0) {
        CHECK(dup2(sv[0], STDERR_FILENO) == STDERR_FILENO);
        cerv_diag_close();
        cerv_diag_prepare();
        CHECK(clock_gettime(CLOCK_MONOTONIC, &before) == 0);
        cerv_diag_message("info", "socket_full", "drop");
        CHECK(clock_gettime(CLOCK_MONOTONIC, &after) == 0);
        CHECK(elapsed_ns(before, after) < UINT64_C(50000000));
        cerv_diag_close();
        CHECK(dup2(saved, STDERR_FILENO) == STDERR_FILENO);
        cerv_diag_prepare();
        CHECK(close(saved) == 0);
    }
    CHECK(close(sv[0]) == 0);
    CHECK(close(sv[1]) == 0);
}

static void test_diag_regular_file(const char *dir)
{
    char path[512];
    int saved = -1;
    int fd = -1;
    struct timespec before;
    struct timespec after;
    CHECK(snprintf(path, sizeof(path), "%s/diag.log", dir) > 0);
    fd = open(path, O_RDWR | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    CHECK(fd >= 0);
    saved = dup(STDERR_FILENO);
    CHECK(saved >= 0);
    if (fd >= 0 && saved >= 0) {
        CHECK(dup2(fd, STDERR_FILENO) == STDERR_FILENO);
        cerv_diag_close();
        cerv_diag_prepare();
        CHECK(clock_gettime(CLOCK_MONOTONIC, &before) == 0);
        cerv_diag_message("info", "regular_file", "drop");
        CHECK(clock_gettime(CLOCK_MONOTONIC, &after) == 0);
        CHECK(elapsed_ns(before, after) < UINT64_C(50000000));
        CHECK(lseek(fd, 0, SEEK_END) == (off_t)0);
        cerv_diag_close();
        CHECK(dup2(saved, STDERR_FILENO) == STDERR_FILENO);
        cerv_diag_prepare();
    }
    if (saved >= 0) CHECK(close(saved) == 0);
    if (fd >= 0) CHECK(close(fd) == 0);
}

static void test_diag_pty(void)
{
    int master = -1;
    int slave = -1;
    int saved = -1;
    char *name;
    char buf[512];
    ssize_t n;
    master = posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC | O_NONBLOCK);
    CHECK(master >= 0);
    if (master < 0) return;
    CHECK(grantpt(master) == 0);
    CHECK(unlockpt(master) == 0);
    name = ptsname(master);
    CHECK(name != NULL);
    if (name == NULL) { (void)close(master); return; }
    slave = open(name, O_WRONLY | O_NOCTTY | O_CLOEXEC);
    CHECK(slave >= 0);
    saved = dup(STDERR_FILENO);
    CHECK(saved >= 0);
    if (slave >= 0 && saved >= 0) {
        CHECK(dup2(slave, STDERR_FILENO) == STDERR_FILENO);
        cerv_diag_close();
        cerv_diag_prepare();
        cerv_diag_message("info", "pty", "ok");
        cerv_diag_close();
        CHECK(dup2(saved, STDERR_FILENO) == STDERR_FILENO);
        cerv_diag_prepare();
        n = read(master, buf, sizeof(buf) - 1U);
        CHECK(n > 0 || (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)));
    }
    if (saved >= 0) CHECK(close(saved) == 0);
    if (slave >= 0) CHECK(close(slave) == 0);
    CHECK(close(master) == 0);
}

int main(void)
{
    char temp[] = "/tmp/cerv-sandbox-XXXXXX";
    char file_path[512];
    char *dir = mkdtemp(temp);
    CHECK(dir != NULL);
    if (dir == NULL) return 1;
    CHECK(snprintf(file_path, sizeof(file_path), "%s/file.txt", dir) > 0);
    CHECK(write_file(file_path, "sandbox\n"));
    cerv_diag_prepare();
#ifndef CERV_INSTRUMENTED_BUILD
    test_worker_seccomp_allowed(dir);
    test_worker_seccomp_forbidden(dir);
    test_landlock(dir);
#endif
    test_diag_full_pipe();
    test_diag_full_socket();
    test_diag_regular_file(dir);
    test_diag_pty();
    cerv_diag_close();
    CHECK(unlink(file_path) == 0);
    {
        char diag_path[512];
        if (snprintf(diag_path, sizeof(diag_path), "%s/diag.log", dir) > 0) (void)unlink(diag_path);
    }
    CHECK(rmdir(dir) == 0);
    if (failures != 0U) {
        fprintf(stderr, "sandbox integration: %u/%u checks failed\n", failures, checks);
        return 1;
    }
    printf("sandbox integration: %u checks passed\n", checks);
    return 0;
}
