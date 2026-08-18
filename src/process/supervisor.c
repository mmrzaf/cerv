#define _GNU_SOURCE
#include "process/supervisor.h"

#include "base/bounds.h"
#include "linux/clock.h"
#include "process/diag.h"
#include "linux/fs.h"
#include "linux/sandbox.h"
#include "runtime/worker.h"
#include "runtime/capacity.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define CERV_CHILD_EXIT_RUNTIME 70
#define CERV_CHILD_EXIT_RESOURCE 71
#define CERV_CHILD_EXIT_STARTUP 72

struct cerv_start_record {
    uint32_t worker_index;
    uint32_t security_flags;
};

#define CERV_START_SECURITY_LANDLOCK UINT32_C(1)

bool cerv_supervisor_resource_plan(const struct cerv_config *config, struct cerv_resource_plan *out)
{
    struct rlimit limit;
    size_t slots;
    if (config == NULL || out == NULL || config->workers == 0U || config->max_connections == 0U ||
        config->workers > config->max_connections ||
        !cerv_config_partition_slots(config->max_connections, config->workers, 0U, &slots) ||
        !cerv_runtime_worker_memory_bytes(slots, &out->worker_memory_bytes) ||
        !cerv_runtime_required_worker_fds(slots, &out->required_worker_fds) ||
        getrlimit(RLIMIT_NOFILE, &limit) != 0) return false;
    out->max_worker_slots = slots;
    out->nofile_soft = limit.rlim_cur;
    if (limit.rlim_cur != RLIM_INFINITY && (uintmax_t)limit.rlim_cur < (uintmax_t)out->required_worker_fds) return false;
    return true;
}

static bool cerv_supervisor_resolve_auto_connections(struct cerv_config *config)
{
    struct rlimit limit;
    uint64_t nofile_soft = UINT64_C(0);
    bool infinite;
    if (config == NULL) return false;
    if (!config->max_connections_auto) return true;
    if (getrlimit(RLIMIT_NOFILE, &limit) != 0) return false;
    infinite = limit.rlim_cur == RLIM_INFINITY;
    if (!infinite) {
        if ((uintmax_t)limit.rlim_cur > (uintmax_t)UINT64_MAX) return false;
        nofile_soft = (uint64_t)limit.rlim_cur;
    }
    return cerv_runtime_auto_connections(config->workers, CERV_DEFAULT_MAX_CONNECTIONS,
                                         nofile_soft, infinite, &config->max_connections);
}

static void cerv_supervisor_diag_resource_failure(const struct cerv_config *config)
{
    struct rlimit limit;
    size_t slots = 0U;
    size_t required = 0U;
    char detail[256];
    const char *soft = "unknown";
    char soft_buf[48];
    if (config == NULL) {
        cerv_diag_message("error", "startup", "resource_limits");
        return;
    }
    if (getrlimit(RLIMIT_NOFILE, &limit) == 0) {
        if (limit.rlim_cur == RLIM_INFINITY) soft = "infinity";
        else {
            (void)snprintf(soft_buf, sizeof(soft_buf), "%ju", (uintmax_t)limit.rlim_cur);
            soft = soft_buf;
        }
    }
    (void)cerv_config_partition_slots(config->max_connections, config->workers, 0U, &slots);
    (void)cerv_runtime_required_worker_fds(slots, &required);
    (void)snprintf(detail, sizeof(detail),
                   "resource_limits workers=%zu max_connections=%zu required_worker_fds=%zu nofile_soft=%s hint=use_max_connections_auto_or_raise_nofile",
                   config->workers, config->max_connections, required, soft);
    cerv_diag_message("error", "startup", detail);
}

static bool cerv_supervisor_signal_mask(sigset_t *mask)
{
    if (mask == NULL || sigemptyset(mask) != 0 || sigaddset(mask, SIGTERM) != 0 ||
        sigaddset(mask, SIGINT) != 0 || sigaddset(mask, SIGCHLD) != 0) return false;
    return sigprocmask(SIG_BLOCK, mask, NULL) == 0;
}

static int cerv_supervisor_open_listener(const struct cerv_config *config)
{
    int fd;
    int one = 1;
    if (config == NULL) return -1;
    fd = socket(config->listen_addr.ss_family, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, (socklen_t)sizeof(one)) != 0 ||
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, (socklen_t)sizeof(one)) != 0) {
        (void)close(fd);
        return -1;
    }
    if (config->listen_addr.ss_family == AF_INET6 &&
        setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &one, (socklen_t)sizeof(one)) != 0) {
        (void)close(fd);
        return -1;
    }
    if (bind(fd, (const struct sockaddr *)(const void *)&config->listen_addr, config->listen_addr_len) != 0 ||
        listen(fd, CERV_LISTEN_BACKLOG) != 0) {
        (void)close(fd);
        return -1;
    }
    return fd;
}

static void cerv_supervisor_diag_backlog(void)
{
    char buf[64];
    char detail[128];
    int fd = open("/proc/sys/net/core/somaxconn", O_RDONLY | O_CLOEXEC);
    ssize_t n = -1;
    if (fd >= 0) {
        do { n = read(fd, buf, sizeof(buf) - 1U); } while (n < 0 && errno == EINTR);
        (void)close(fd);
    }
    if (n > 0) {
        size_t len = (size_t)n;
        while (len > 0U && (buf[len - 1U] == '\n' || buf[len - 1U] == '\r')) --len;
        buf[len] = '\0';
        (void)snprintf(detail, sizeof(detail), "requested=%d somaxconn=%s", CERV_LISTEN_BACKLOG, buf);
    } else {
        (void)snprintf(detail, sizeof(detail), "requested=%d somaxconn=unknown", CERV_LISTEN_BACKLOG);
    }
    cerv_diag_message("info", "listen_backlog", detail);
}

static bool cerv_fd_make_blocking(int fd)
{
    int flags;
    if (fd < 0) return false;
    flags = fcntl(fd, F_GETFL);
    if (flags < 0) return false;
    if ((flags & O_NONBLOCK) == 0) return true;
    return fcntl(fd, F_SETFL, flags & ~O_NONBLOCK) == 0;
}

static bool cerv_write_start_record(int fd, size_t worker_index, enum cerv_landlock_status landlock_status)
{
    struct cerv_start_record record;
    const unsigned char *p = (const unsigned char *)(const void *)&record;
    size_t done = 0U;
    if (worker_index > (size_t)UINT32_MAX) return false;
    record.worker_index = (uint32_t)worker_index;
    record.security_flags = landlock_status == CERV_LANDLOCK_ENABLED ? CERV_START_SECURITY_LANDLOCK : UINT32_C(0);
    while (done < sizeof(record)) {
        ssize_t n = write(fd, p + done, sizeof(record) - done);
        if (n > 0) done += (size_t)n;
        else if (n < 0 && errno == EINTR) continue;
        else return false;
    }
    return true;
}

static bool cerv_worker_signal_fd(int *out)
{
    sigset_t mask;
    if (out == NULL || sigemptyset(&mask) != 0 || sigaddset(&mask, SIGTERM) != 0 || sigaddset(&mask, SIGINT) != 0) {
        return false;
    }
    *out = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
    return *out >= 0;
}

static bool cerv_worker_consume_control(int signal_fd, bool *draining, struct cerv_worker *worker)
{
    struct signalfd_siginfo info[8];
    for (;;) {
        ssize_t n = read(signal_fd, info, sizeof(info));
        size_t count;
        size_t i;
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) return true;
            return false;
        }
        if (n == 0 || (size_t)n % sizeof(info[0]) != 0U) return false;
        count = (size_t)n / sizeof(info[0]);
        for (i = 0U; i < count; ++i) {
            if (info[i].ssi_signo != (uint32_t)SIGTERM && info[i].ssi_signo != (uint32_t)SIGINT) return false;
            if (!*draining) {
                if (!cerv_worker_stop_accepting(worker)) return false;
                *draining = true;
            }
        }
    }
}

static int cerv_worker_child(size_t worker_index, size_t slots_count, int listener_fd,
                             struct cerv_fs_root *root, int master_signal_fd,
                             int startup_read_fd, int startup_write_fd,
                             const struct cerv_config *config)
{
    struct cerv_conn *slots = NULL;
    struct cerv_timer_node *timers = NULL;
    struct cerv_worker worker;
    struct cerv_worker_config worker_config;
    int signal_fd = -1;
    int exit_code = CERV_CHILD_EXIT_STARTUP;
    bool worker_live = false;
    bool draining = false;
    enum cerv_landlock_status landlock_status = CERV_LANDLOCK_UNAVAILABLE;
    (void)close(startup_read_fd);
    (void)close(master_signal_fd);
    cerv_diag_close();
    if (!cerv_worker_signal_fd(&signal_fd)) goto done;
    slots = calloc(slots_count, sizeof(*slots));
    timers = calloc(slots_count, sizeof(*timers));
    if (slots == NULL || timers == NULL) goto done;
    worker_config.header_timeout = config->header_timeout;
    worker_config.write_timeout = config->write_timeout;
    worker_config.max_lifetime = config->max_lifetime;
    worker_config.spa_fallback = (struct cerv_path){0};
    if (config->spa_fallback_len != 0U) {
        memcpy(worker_config.spa_fallback.bytes, config->spa_fallback, config->spa_fallback_len + 1U);
        worker_config.spa_fallback.len = config->spa_fallback_len;
    }
    worker_config.immutable = config->immutable;
    worker_config.shared_listener_cooperative = config->workers > 1U;
    if (!cerv_worker_init(&worker, listener_fd, root, slots, timers, slots_count, worker_config)) goto done;
    worker_live = true;
    if (!cerv_worker_set_control_fd(&worker, signal_fd)) goto done;
    /* The parent keeps the readiness read end nonblocking.  The child uses a
       blocking write end so a tiny pipe cannot turn bounded startup fan-out
       into a spurious EAGAIN failure before the master begins draining it. */
    if (!cerv_fd_make_blocking(startup_write_fd)) goto done;
    if (!cerv_sandbox_worker_enter(root, &landlock_status)) goto done;
    if (!cerv_write_start_record(startup_write_fd, worker_index, landlock_status)) goto done;
    if (close(startup_write_fd) != 0) goto done;
    startup_write_fd = -1;
    for (;;) {
        enum cerv_worker_result result;
        if (draining && cerv_worker_active_connections(&worker) == 0U) {
            exit_code = 0;
            break;
        }
        result = cerv_worker_run_once(&worker, -1);
        if (result == CERV_WORKER_CONTROL_READY) {
            if (!cerv_worker_consume_control(signal_fd, &draining, &worker)) {
                exit_code = CERV_CHILD_EXIT_RUNTIME;
                break;
            }
            continue;
        }
        if (result == CERV_WORKER_RESOURCE_EXHAUSTED) {
            exit_code = CERV_CHILD_EXIT_RESOURCE;
            break;
        }
        if (result != CERV_WORKER_OK) {
            exit_code = CERV_CHILD_EXIT_RUNTIME;
            break;
        }
    }
done:
    if (worker_live) cerv_worker_destroy(&worker);
    if (signal_fd >= 0) (void)close(signal_fd);
    if (startup_write_fd >= 0) (void)close(startup_write_fd);
    free(timers);
    free(slots);
    (void)close(listener_fd);
    cerv_fs_root_close(root);
    return exit_code;
}

static void cerv_kill_alive(const pid_t pids[], const bool alive[], size_t count, int signal_number)
{
    size_t i;
    for (i = 0U; i < count; ++i) {
        if (alive[i] && pids[i] > (pid_t)0) (void)kill(pids[i], signal_number);
    }
}

static bool cerv_find_worker(const pid_t pids[], size_t count, pid_t pid, size_t *out)
{
    size_t i;
    for (i = 0U; i < count; ++i) {
        if (pids[i] == pid) { *out = i; return true; }
    }
    return false;
}

static size_t cerv_reap_children(const pid_t pids[], bool alive[], size_t count, bool draining,
                                 bool hard_sent, bool *fatal)
{
    size_t reaped = 0U;
    for (;;) {
        int status;
        pid_t pid = waitpid((pid_t)-1, &status, WNOHANG);
        size_t index;
        if (pid == (pid_t)0) break;
        if (pid < (pid_t)0) {
            if (errno == EINTR) continue;
            if (errno == ECHILD) break;
            *fatal = true;
            break;
        }
        if (!cerv_find_worker(pids, count, pid, &index) || !alive[index]) {
            *fatal = true;
            continue;
        }
        alive[index] = false;
        ++reaped;
        if (!draining) {
            cerv_diag_worker_exit(pid, status);
            *fatal = true;
        } else if (!(WIFEXITED(status) && WEXITSTATUS(status) == 0) &&
                   !(hard_sent && WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL)) {
            cerv_diag_worker_exit(pid, status);
            *fatal = true;
        }
    }
    return reaped;
}

static size_t cerv_alive_count(const bool alive[], size_t count)
{
    size_t i;
    size_t total = 0U;
    for (i = 0U; i < count; ++i) if (alive[i]) ++total;
    return total;
}

static void cerv_force_reap_workers(const pid_t pids[], bool alive[], size_t count, bool *fatal)
{
    if (pids == NULL || alive == NULL || fatal == NULL) return;
    if (cerv_alive_count(alive, count) == 0U) return;
    cerv_kill_alive(pids, alive, count, SIGKILL);
    while (cerv_alive_count(alive, count) != 0U) {
        (void)cerv_reap_children(pids, alive, count, true, true, fatal);
        if (cerv_alive_count(alive, count) != 0U) (void)poll(NULL, 0, 1);
    }
}

static int cerv_deadline_poll_timeout(bool draining, struct cerv_mono_time deadline)
{
    struct cerv_mono_time now;
    uint64_t ms;
    if (!draining) return -1;
    if (!cerv_clock_mono_now(&now)) return 0;
    ms = cerv_mono_remaining_ms_ceil(now, deadline);
    return ms > (uint64_t)INT_MAX ? INT_MAX : (int)ms;
}

static bool cerv_deadline_expired(struct cerv_mono_time deadline)
{
    struct cerv_mono_time now;
    return !cerv_clock_mono_now(&now) || now.ns >= deadline.ns;
}

static bool cerv_start_draining(const pid_t pids[], const bool alive[], size_t count,
                                struct cerv_duration timeout, struct cerv_mono_time *deadline)
{
    struct cerv_mono_time now;
    if (!cerv_clock_mono_now(&now)) return false;
    *deadline = cerv_mono_add_saturating(now, timeout);
    cerv_kill_alive(pids, alive, count, SIGTERM);
    return true;
}

bool cerv_supervisor_wait_startup(int signal_fd, int pipe_fd, const pid_t pids[], bool alive[], size_t worker_count,
                                  struct cerv_duration timeout, bool *landlock_all)
{
    bool seen[CERV_WORKERS_MAX] = {false};
    size_t ready = 0U;
    struct cerv_mono_time now;
    struct cerv_mono_time deadline;
    if (landlock_all == NULL || timeout.ns == UINT64_C(0) || !cerv_clock_mono_now(&now)) return false;
    *landlock_all = true;
    deadline = cerv_mono_add_saturating(now, timeout);
    while (ready < worker_count) {
        struct pollfd fds[2] = {
            {.fd = signal_fd, .events = POLLIN, .revents = 0},
            {.fd = pipe_fd, .events = POLLIN, .revents = 0}
        };
        int polled = poll(fds, 2, cerv_deadline_poll_timeout(true, deadline));
        if (polled < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (polled == 0) return false;
        if ((fds[0].revents & POLLIN) != 0) {
            struct signalfd_siginfo infos[8];
            ssize_t n;
            do { n = read(signal_fd, infos, sizeof(infos)); } while (n < 0 && errno == EINTR);
            if (n <= 0 || (size_t)n % sizeof(infos[0]) != 0U) return false;
            {
                size_t count = (size_t)n / sizeof(infos[0]);
                size_t i;
                for (i = 0U; i < count; ++i) {
                    if (infos[i].ssi_signo == (uint32_t)SIGCHLD) {
                        bool fatal = false;
                        (void)cerv_reap_children(pids, alive, worker_count, false, false, &fatal);
                        if (fatal) return false;
                    } else if (infos[i].ssi_signo == (uint32_t)SIGTERM || infos[i].ssi_signo == (uint32_t)SIGINT) {
                        return false;
                    }
                }
            }
        }
        if ((fds[1].revents & (POLLIN | POLLHUP)) != 0) {
            for (;;) {
                struct cerv_start_record record;
                ssize_t n = read(pipe_fd, &record, sizeof(record));
                if (n == (ssize_t)sizeof(record)) {
                    if ((size_t)record.worker_index >= worker_count || seen[record.worker_index]) return false;
                    if ((record.security_flags & ~CERV_START_SECURITY_LANDLOCK) != UINT32_C(0)) return false;
                    if ((record.security_flags & CERV_START_SECURITY_LANDLOCK) == UINT32_C(0)) *landlock_all = false;
                    seen[record.worker_index] = true;
                    ++ready;
                    continue;
                }
                if (n < 0 && errno == EINTR) continue;
                if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
                if (n == 0) return ready == worker_count;
                return false;
            }
        }
    }
    return true;
}

int cerv_supervisor_run(const struct cerv_config *config)
{
    struct cerv_config resolved_config;
    const struct cerv_config *run_config;
    struct cerv_fs_root root = {.fd = -1};
    struct cerv_resource_plan plan;
    sigset_t mask;
    pid_t pids[CERV_WORKERS_MAX] = {0};
    bool alive[CERV_WORKERS_MAX] = {false};
    int startup_pipe[2] = {-1, -1};
    int signal_fd = -1;
    int listener_fd = -1;
    size_t created = 0U;
    bool draining = false;
    bool fatal = false;
    bool hard_sent = false;
    struct cerv_mono_time shutdown_deadline = {.ns = UINT64_C(0)};
    int exit_code = 1;
    size_t i;
    bool landlock_all = false;
    struct cerv_duration startup_timeout;
    cerv_diag_prepare();
    if (config == NULL || config->workers == 0U || config->workers > CERV_WORKERS_MAX ||
        !cerv_duration_from_ms(CERV_STARTUP_READY_TIMEOUT_MS, &startup_timeout)) return 1;
    resolved_config = *config;
    if (!cerv_supervisor_resolve_auto_connections(&resolved_config)) {
        cerv_supervisor_diag_resource_failure(&resolved_config);
        return 1;
    }
    run_config = &resolved_config;
    if (!cerv_worker_prepare_process() || !cerv_supervisor_signal_mask(&mask)) {
        cerv_diag_message("error", "startup", "signal_policy");
        return 1;
    }
    signal_fd = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
    if (signal_fd < 0) { cerv_diag_message("error", "startup", "signalfd"); goto done; }
    if (cerv_fs_root_open(run_config->root_path, &root) != CERV_FS_ROOT_OK) {
        cerv_diag_message("error", "startup", "document_root"); goto done;
    }
    if (!cerv_supervisor_resource_plan(run_config, &plan)) {
        cerv_supervisor_diag_resource_failure(run_config); goto done;
    }
    listener_fd = cerv_supervisor_open_listener(run_config);
    if (listener_fd < 0) { cerv_diag_message("error", "startup", "bind_listen"); goto done; }
    if (pipe2(startup_pipe, O_CLOEXEC | O_NONBLOCK) != 0) {
        cerv_diag_message("error", "startup", "worker_ready_pipe"); goto done;
    }
    cerv_diag_startup(run_config->workers, run_config->max_connections, plan.max_worker_slots,
                      plan.worker_memory_bytes, plan.required_worker_fds);
    cerv_supervisor_diag_backlog();
    for (i = 0U; i < run_config->workers; ++i) {
        size_t slots_count;
        pid_t pid;
        if (!cerv_config_partition_slots(run_config->max_connections, run_config->workers, i, &slots_count)) goto startup_fail;
        pid = fork();
        if (pid < (pid_t)0) goto startup_fail;
        if (pid == (pid_t)0) {
            int child_code = cerv_worker_child(i, slots_count, listener_fd, &root, signal_fd,
                                               startup_pipe[0], startup_pipe[1], run_config);
            _exit(child_code);
        }
        pids[i] = pid;
        alive[i] = true;
        ++created;
    }
    (void)close(startup_pipe[1]);
    startup_pipe[1] = -1;
    if (!cerv_supervisor_wait_startup(signal_fd, startup_pipe[0], pids, alive, run_config->workers,
                                      startup_timeout, &landlock_all)) goto startup_fail;
    (void)close(startup_pipe[0]);
    startup_pipe[0] = -1;
    (void)close(listener_fd);
    listener_fd = -1;
    cerv_fs_root_close(&root);
    if (!cerv_sandbox_master_enter()) {
        cerv_diag_message("error", "startup", "master_seccomp");
        goto startup_fail;
    }
    cerv_diag_message("info", "sandbox", landlock_all ? "seccomp=on landlock=on" : "seccomp=on landlock=unavailable");
    cerv_diag_message("info", "ready", "workers_started");
    while (cerv_alive_count(alive, run_config->workers) != 0U) {
        struct pollfd pfd = {.fd = signal_fd, .events = POLLIN, .revents = 0};
        int timeout = cerv_deadline_poll_timeout(draining, shutdown_deadline);
        int polled = poll(&pfd, 1, timeout);
        if (polled < 0) {
            if (errno == EINTR) continue;
            fatal = true;
            if (!draining) {
                draining = true;
                if (!cerv_start_draining(pids, alive, run_config->workers, run_config->shutdown_timeout, &shutdown_deadline)) break;
            }
            continue;
        }
        if (polled == 0) {
            if (draining && !hard_sent && cerv_deadline_expired(shutdown_deadline)) {
                cerv_diag_message("error", "shutdown_hard", "deadline_expired");
                cerv_kill_alive(pids, alive, run_config->workers, SIGKILL);
                hard_sent = true;
            }
            continue;
        }
        if ((pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
            fatal = true;
            if (!draining) {
                draining = true;
                if (!cerv_start_draining(pids, alive, run_config->workers, run_config->shutdown_timeout, &shutdown_deadline)) break;
            }
        }
        if ((pfd.revents & POLLIN) != 0) {
            struct signalfd_siginfo infos[16];
            ssize_t n;
            do { n = read(signal_fd, infos, sizeof(infos)); } while (n < 0 && errno == EINTR);
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) continue;
            if (n <= 0 || (size_t)n % sizeof(infos[0]) != 0U) { fatal = true; break; }
            {
                size_t count = (size_t)n / sizeof(infos[0]);
                size_t j;
                for (j = 0U; j < count; ++j) {
                    uint32_t signo = infos[j].ssi_signo;
                    if (signo == (uint32_t)SIGCHLD) {
                        size_t before = cerv_alive_count(alive, run_config->workers);
                        (void)cerv_reap_children(pids, alive, run_config->workers, draining, hard_sent, &fatal);
                        if (!draining && cerv_alive_count(alive, run_config->workers) < before) {
                            draining = true;
                            if (!cerv_start_draining(pids, alive, run_config->workers, run_config->shutdown_timeout,
                                                     &shutdown_deadline)) fatal = true;
                        }
                    } else if (signo == (uint32_t)SIGTERM || signo == (uint32_t)SIGINT) {
                        if (!draining) {
                            cerv_diag_message("info", "shutdown_start", signo == (uint32_t)SIGTERM ? "SIGTERM" : "SIGINT");
                            draining = true;
                            if (!cerv_start_draining(pids, alive, run_config->workers, run_config->shutdown_timeout,
                                                     &shutdown_deadline)) fatal = true;
                        } else if (!hard_sent) {
                            cerv_diag_message("error", "shutdown_hard", "second_signal");
                            cerv_kill_alive(pids, alive, run_config->workers, SIGKILL);
                            hard_sent = true;
                        }
                    }
                }
            }
        }
        if (draining && !hard_sent && cerv_deadline_expired(shutdown_deadline) && cerv_alive_count(alive, run_config->workers) != 0U) {
            cerv_diag_message("error", "shutdown_hard", "deadline_expired");
            cerv_kill_alive(pids, alive, run_config->workers, SIGKILL);
            hard_sent = true;
        }
    }
    if (cerv_alive_count(alive, run_config->workers) == 0U) {
        exit_code = fatal ? 1 : 0;
        cerv_diag_message(fatal ? "error" : "info", "shutdown_end", fatal ? "fatal" : "clean");
    }
    goto done;
startup_fail:
    cerv_diag_message("error", "startup", "worker_creation_or_readiness");
    fatal = true;
    cerv_force_reap_workers(pids, alive, created, &fatal);
done:
    cerv_force_reap_workers(pids, alive, created, &fatal);
    if (startup_pipe[0] >= 0) (void)close(startup_pipe[0]);
    if (startup_pipe[1] >= 0) (void)close(startup_pipe[1]);
    if (listener_fd >= 0) (void)close(listener_fd);
    cerv_fs_root_close(&root);
    if (signal_fd >= 0) (void)close(signal_fd);
    return exit_code;
}
