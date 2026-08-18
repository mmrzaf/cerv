#define _GNU_SOURCE 1
#include "process/diag.h"

#include "base/bounds.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static int cerv_diag_fd = -1;

static int cerv_diag_open_nonblocking_clone(void)
{
    int fd;
    int flags;
    struct stat st;

    fd = open("/proc/self/fd/2", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd >= 0) {
        if (fstat(fd, &st) == 0 && !S_ISREG(st.st_mode)) return fd;
        (void)close(fd);
    }

    if (fstat(STDERR_FILENO, &st) != 0 || S_ISREG(st.st_mode)) return -1;
    flags = fcntl(STDERR_FILENO, F_GETFL);
    if (flags < 0 || (flags & O_NONBLOCK) == 0) return -1;
    return fcntl(STDERR_FILENO, F_DUPFD_CLOEXEC, 3);
}

void cerv_diag_prepare(void)
{
    if (cerv_diag_fd >= 0) return;
    cerv_diag_fd = cerv_diag_open_nonblocking_clone();
}

void cerv_diag_close(void)
{
    if (cerv_diag_fd >= 0) {
        (void)close(cerv_diag_fd);
        cerv_diag_fd = -1;
    }
}

static void cerv_diag_write(const char *buf, size_t len)
{
    ssize_t ignored;
    if (buf == NULL || len == 0U || cerv_diag_fd < 0) return;
    /* One nonblocking write attempt only. Partial/EINTR/EAGAIN output is dropped. */
    ignored = write(cerv_diag_fd, buf, len);
    (void)ignored;
}

void cerv_diag_message(const char *level, const char *event, const char *detail)
{
    char buf[CERV_DIAG_BYTES_MAX];
    int n;
    const char *safe_level = level == NULL ? "info" : level;
    const char *safe_event = event == NULL ? "event" : event;
    const char *safe_detail = detail == NULL ? "" : detail;
    n = snprintf(buf, sizeof(buf), "cerv level=%s event=%s detail=%s\n", safe_level, safe_event, safe_detail);
    if (n < 0) return;
    if ((size_t)n >= sizeof(buf)) {
        const char suffix[] = "...\n";
        size_t suffix_len = sizeof(suffix) - 1U;
        memcpy(buf + sizeof(buf) - suffix_len - 1U, suffix, suffix_len);
        buf[sizeof(buf) - 1U] = '\0';
        cerv_diag_write(buf, sizeof(buf) - 1U);
        return;
    }
    cerv_diag_write(buf, (size_t)n);
}

void cerv_diag_worker_exit(pid_t pid, int status)
{
    char detail[128];
    if (WIFEXITED(status)) {
        (void)snprintf(detail, sizeof(detail), "pid=%ld exit=%d", (long)pid, WEXITSTATUS(status));
    } else if (WIFSIGNALED(status)) {
        (void)snprintf(detail, sizeof(detail), "pid=%ld signal=%d", (long)pid, WTERMSIG(status));
    } else {
        (void)snprintf(detail, sizeof(detail), "pid=%ld status=0x%x", (long)pid, status);
    }
    cerv_diag_message("error", "worker_exit", detail);
}

void cerv_diag_startup(size_t workers, size_t max_connections, size_t max_worker_slots,
                       size_t worker_memory_bytes, size_t required_worker_fds)
{
    char detail[256];
    (void)snprintf(detail, sizeof(detail),
                   "workers=%zu max_connections=%zu max_worker_slots=%zu worker_state_bytes=%zu required_worker_fds=%zu access_log=off",
                   workers, max_connections, max_worker_slots, worker_memory_bytes, required_worker_fds);
    cerv_diag_message("info", "startup", detail);
}
