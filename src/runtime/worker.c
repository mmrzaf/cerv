#define _GNU_SOURCE
#include "runtime/worker.h"

#include "base/bounds.h"
#include "linux/clock.h"
#include "serve/response.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <string.h>
#include <unistd.h>

#define CERV_LISTENER_TOKEN UINT64_MAX
#define CERV_CONTROL_TOKEN UINT64_C(0x00000000ffffffff)

bool cerv_worker_prepare_process(void)
{
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = SIG_IGN;
    if (sigemptyset(&action.sa_mask) != 0) return false;
    return sigaction(SIGPIPE, &action, NULL) == 0;
}

bool cerv_worker_config_default(struct cerv_worker_config *out)
{
    if (out == NULL || !cerv_duration_from_ms(CERV_DEFAULT_HEADER_TIMEOUT_MS, &out->header_timeout) ||
        !cerv_duration_from_ms(CERV_DEFAULT_WRITE_TIMEOUT_MS, &out->write_timeout) ||
        !cerv_duration_from_ms(CERV_DEFAULT_MAX_LIFETIME_MS, &out->max_lifetime)) return false;
    out->spa_fallback = (struct cerv_path){0};
    out->immutable = false;
    out->shared_listener_cooperative = false;
    return true;
}

static bool cerv_worker_listener_valid(int fd)
{
    int flags;
    int fd_flags;
    int accepting = 0;
    int socket_type = 0;
    socklen_t accepting_len = (socklen_t)sizeof(accepting);
    socklen_t type_len = (socklen_t)sizeof(socket_type);
    if (fd < 0) return false;
    flags = fcntl(fd, F_GETFL);
    fd_flags = fcntl(fd, F_GETFD);
    if (flags < 0 || fd_flags < 0 || (flags & O_NONBLOCK) == 0 || (fd_flags & FD_CLOEXEC) == 0) return false;
    if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &socket_type, &type_len) != 0 || socket_type != SOCK_STREAM) return false;
    if (getsockopt(fd, SOL_SOCKET, SO_ACCEPTCONN, &accepting, &accepting_len) != 0 || accepting == 0) return false;
    return true;
}

static uint32_t cerv_worker_conn_events(const struct cerv_conn *conn)
{
    if (cerv_conn_wants_read(conn)) return (uint32_t)(EPOLLIN | EPOLLRDHUP);
    if (cerv_conn_wants_write(conn)) return (uint32_t)(EPOLLOUT | EPOLLRDHUP);
    return UINT32_C(0);
}

static bool cerv_worker_listener_add(struct cerv_worker *worker)
{
    struct epoll_event event;
    if (worker == NULL || worker->epoll_fd < 0 || !worker->accepting || worker->listener_registered) return false;
    event = (struct epoll_event){.events = (uint32_t)(EPOLLIN | EPOLLEXCLUSIVE), .data.u64 = CERV_LISTENER_TOKEN};
    if (epoll_ctl(worker->epoll_fd, EPOLL_CTL_ADD, worker->listener_fd, &event) != 0) return false;
    worker->listener_registered = true;
    return true;
}

static bool cerv_worker_listener_remove(struct cerv_worker *worker)
{
    if (worker == NULL || worker->epoll_fd < 0) return false;
    if (!worker->listener_registered) return true;
    if (epoll_ctl(worker->epoll_fd, EPOLL_CTL_DEL, worker->listener_fd, NULL) != 0 && errno != ENOENT) return false;
    worker->listener_registered = false;
    return true;
}

bool cerv_worker_init(struct cerv_worker *worker, int listener_fd, const struct cerv_fs_root *root,
                      struct cerv_conn *slots, struct cerv_timer_node *timer_storage, size_t slot_count,
                      struct cerv_worker_config config)
{
    int epoll_fd;
    if (worker == NULL || root == NULL || root->fd < 0 || !cerv_worker_listener_valid(listener_fd) ||
        slots == NULL || timer_storage == NULL || slot_count == 0U || slot_count > (size_t)UINT32_MAX ||
        config.header_timeout.ns == 0U || config.write_timeout.ns == 0U || config.max_lifetime.ns == 0U) return false;
    if (!cerv_worker_prepare_process()) return false;
    *worker = (struct cerv_worker){.epoll_fd = -1, .listener_fd = listener_fd, .control_fd = -1, .root = root, .config = config};
    if (!cerv_conn_arena_init(&worker->arena, slots, slot_count) ||
        !cerv_timer_heap_init(&worker->timers, timer_storage, slot_count)) return false;
    epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd < 0) return false;
    worker->epoll_fd = epoll_fd;
    worker->accepting = true;
    worker->listener_registered = false;
    if (!cerv_worker_listener_add(worker)) {
        (void)close(worker->epoll_fd);
        worker->epoll_fd = -1;
        worker->accepting = false;
        return false;
    }
    return true;
}

static bool cerv_worker_timer_sync(struct cerv_worker *worker, struct cerv_conn *conn)
{
    struct cerv_mono_time deadline;
    if (!cerv_conn_next_deadline(conn, &deadline)) return false;
    return cerv_timer_set(&worker->timers, &conn->timer, conn->index, conn->generation, deadline.ns);
}

static bool cerv_worker_epoll_sync(struct cerv_worker *worker, struct cerv_conn *conn)
{
    struct epoll_event event;
    uint32_t events = cerv_worker_conn_events(conn);
    if (events == 0U) return false;
    event = (struct epoll_event){.events = events, .data.u64 = cerv_conn_token(conn)};
    return epoll_ctl(worker->epoll_fd, EPOLL_CTL_MOD, conn->socket_fd, &event) == 0;
}

static bool cerv_worker_release_conn(struct cerv_worker *worker, struct cerv_conn *conn)
{
    int socket_fd;
    if (worker == NULL || conn == NULL) return false;
    if (!cerv_timer_remove(&worker->timers, &conn->timer)) return false;
    socket_fd = conn->socket_fd;
    if (socket_fd >= 0) (void)epoll_ctl(worker->epoll_fd, EPOLL_CTL_DEL, socket_fd, NULL);
    cerv_conn_cleanup(conn);
    if (!cerv_conn_arena_release(&worker->arena, conn)) return false;
    if (worker->accepting && !worker->listener_registered && worker->arena.active < worker->arena.capacity) {
        return cerv_worker_listener_add(worker);
    }
    return true;
}

void cerv_worker_destroy(struct cerv_worker *worker)
{
    size_t i;
    if (worker == NULL) return;
    if (worker->arena.slots != NULL) {
        for (i = 0U; i < worker->arena.capacity; ++i) {
            struct cerv_conn *conn = &worker->arena.slots[i];
            if (conn->state != CERV_CONN_FREE) {
                (void)cerv_timer_remove(&worker->timers, &conn->timer);
                if (worker->epoll_fd >= 0 && conn->socket_fd >= 0) {
                    (void)epoll_ctl(worker->epoll_fd, EPOLL_CTL_DEL, conn->socket_fd, NULL);
                }
                cerv_conn_cleanup(conn);
            }
        }
    }
    if (worker->epoll_fd >= 0) (void)close(worker->epoll_fd);
    worker->epoll_fd = -1;
    worker->control_fd = -1;
    worker->accepting = false;
    worker->listener_registered = false;
    worker->arena.active = 0U;
}

bool cerv_worker_stop_accepting(struct cerv_worker *worker)
{
    size_t i;
    if (worker == NULL || worker->epoll_fd < 0) return false;
    if (!worker->accepting) return true;
    if (!cerv_worker_listener_remove(worker)) return false;
    worker->accepting = false;
    /* Draining also retires persistent sockets: finish at most the current/next response. */
    for (i = 0U; i < worker->arena.capacity; ++i) {
        struct cerv_conn *conn = &worker->arena.slots[i];
        if (conn->state != CERV_CONN_FREE) conn->close_after_response = true;
    }
    return true;
}

bool cerv_worker_set_control_fd(struct cerv_worker *worker, int control_fd)
{
    struct epoll_event event;
    int flags;
    int fd_flags;
    if (worker == NULL || worker->epoll_fd < 0 || control_fd < 0 || worker->control_fd >= 0 ||
        control_fd == worker->listener_fd) return false;
    flags = fcntl(control_fd, F_GETFL);
    fd_flags = fcntl(control_fd, F_GETFD);
    if (flags < 0 || fd_flags < 0 || (flags & O_NONBLOCK) == 0 || (fd_flags & FD_CLOEXEC) == 0) return false;
    event = (struct epoll_event){.events = (uint32_t)EPOLLIN, .data.u64 = CERV_CONTROL_TOKEN};
    if (epoll_ctl(worker->epoll_fd, EPOLL_CTL_ADD, control_fd, &event) != 0) return false;
    worker->control_fd = control_fd;
    return true;
}

size_t cerv_worker_active_connections(const struct cerv_worker *worker)
{
    return worker == NULL ? 0U : worker->arena.active;
}

static bool cerv_accept_transient_error(int error_number)
{
    switch (error_number) {
    case ECONNABORTED:
    case ENETDOWN:
    case EPROTO:
    case ENOPROTOOPT:
    case EHOSTDOWN:
    case ENONET:
    case EHOSTUNREACH:
    case EOPNOTSUPP:
    case ENETUNREACH:
    case EPERM:
        return true;
    default:
        return false;
    }
}

static bool cerv_accept_resource_error(int error_number)
{
    return error_number == EMFILE || error_number == ENFILE || error_number == ENOBUFS || error_number == ENOMEM;
}

static bool cerv_worker_overload_reject(int fd)
{
    struct cerv_response_plan plan;
    unsigned char wire[CERV_RESPONSE_BYTES_MAX + CERV_ERROR_BODY_MAX];
    size_t wire_len;
    int64_t unix_seconds;
    if (!cerv_clock_unix_now(&unix_seconds) || !cerv_response_plan_error(CERV_STATUS_503, false, unix_seconds, &plan)) {
        return false;
    }
    if (plan.header_len > CERV_RESPONSE_BYTES_MAX || plan.body_len > CERV_ERROR_BODY_MAX ||
        plan.header_len > sizeof(wire) - plan.body_len) return false;
    memcpy(wire, plan.headers, plan.header_len);
    wire_len = plan.header_len;
    if (plan.send_body && plan.body_len != 0U) {
        memcpy(wire + wire_len, plan.body, plan.body_len);
        wire_len += plan.body_len;
    }
    (void)send(fd, wire, wire_len, MSG_NOSIGNAL | MSG_DONTWAIT);
    return true;
}

static enum cerv_worker_result cerv_worker_admit(struct cerv_worker *worker, int fd, struct cerv_mono_time now)
{
    struct cerv_conn *conn = NULL;
    enum cerv_slot_acquire_result acquired = cerv_conn_arena_acquire(&worker->arena, &conn);
    struct epoll_event event;
    if (acquired == CERV_SLOT_ACQUIRE_FULL) {
        bool ok = cerv_worker_overload_reject(fd);
        (void)close(fd);
        if (!ok || !cerv_worker_listener_remove(worker)) return CERV_WORKER_FATAL;
        return CERV_WORKER_OK;
    }
    if (acquired != CERV_SLOT_ACQUIRE_OK || conn == NULL) {
        (void)close(fd);
        return CERV_WORKER_FATAL;
    }
    if (!cerv_conn_begin(conn, fd, now, worker->config.header_timeout, worker->config.max_lifetime)) {
        (void)close(fd);
        conn->socket_fd = -1;
        conn->state = CERV_CONN_FREE;
        if (!cerv_conn_arena_release(&worker->arena, conn)) return CERV_WORKER_FATAL;
        return CERV_WORKER_FATAL;
    }
    event = (struct epoll_event){.events = cerv_worker_conn_events(conn), .data.u64 = cerv_conn_token(conn)};
    if (epoll_ctl(worker->epoll_fd, EPOLL_CTL_ADD, fd, &event) != 0) {
        int error_number = errno;
        cerv_conn_cleanup(conn);
        if (!cerv_conn_arena_release(&worker->arena, conn)) return CERV_WORKER_FATAL;
        return (error_number == ENOSPC || error_number == ENOMEM) ? CERV_WORKER_RESOURCE_EXHAUSTED : CERV_WORKER_FATAL;
    }
    if (!cerv_worker_timer_sync(worker, conn)) {
        (void)cerv_worker_release_conn(worker, conn);
        return CERV_WORKER_FATAL;
    }
    return CERV_WORKER_OK;
}

static enum cerv_worker_result cerv_worker_listener_yield(struct cerv_worker *worker, bool accepted_any)
{
    if (!accepted_any || !worker->config.shared_listener_cooperative || !worker->accepting ||
        !worker->listener_registered) return CERV_WORKER_OK;
    if (!cerv_worker_listener_remove(worker)) return CERV_WORKER_FATAL;
    if (worker->arena.active >= worker->arena.capacity) return CERV_WORKER_OK;
    return cerv_worker_listener_add(worker) ? CERV_WORKER_OK : CERV_WORKER_FATAL;
}

static enum cerv_worker_result cerv_worker_accept_ready(struct cerv_worker *worker)
{
    size_t attempts = 0U;
    bool accepted_any = false;
    while (attempts < CERV_ACCEPT_QUANTUM && worker->accepting && worker->listener_registered) {
        int fd;
        struct cerv_mono_time now;
        if (worker->config.shared_listener_cooperative && worker->arena.active >= worker->arena.capacity) {
            return cerv_worker_listener_remove(worker) ? CERV_WORKER_OK : CERV_WORKER_FATAL;
        }
        ++attempts;
        fd = accept4(worker->listener_fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (fd >= 0) {
            enum cerv_worker_result admitted;
            if (!cerv_clock_mono_now(&now)) {
                (void)close(fd);
                return CERV_WORKER_FATAL;
            }
            admitted = cerv_worker_admit(worker, fd, now);
            if (admitted != CERV_WORKER_OK) return admitted;
            accepted_any = true;
            continue;
        }
        if (errno == EINTR || cerv_accept_transient_error(errno)) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return cerv_worker_listener_yield(worker, accepted_any);
        if (cerv_accept_resource_error(errno)) return CERV_WORKER_RESOURCE_EXHAUSTED;
        return CERV_WORKER_FATAL;
    }
    return cerv_worker_listener_yield(worker, accepted_any);
}

static enum cerv_worker_result cerv_worker_handle_conn_result(struct cerv_worker *worker, struct cerv_conn *conn,
                                                              enum cerv_conn_result result)
{
    if (result == CERV_CONN_FATAL) return CERV_WORKER_FATAL;
    if (result == CERV_CONN_CLOSE) {
        return cerv_worker_release_conn(worker, conn) ? CERV_WORKER_OK : CERV_WORKER_FATAL;
    }
    if (!cerv_worker_epoll_sync(worker, conn) || !cerv_worker_timer_sync(worker, conn)) return CERV_WORKER_FATAL;
    return CERV_WORKER_OK;
}

static enum cerv_worker_result cerv_worker_dispatch_conn(struct cerv_worker *worker, uint64_t token, uint32_t events)
{
    struct cerv_conn *conn = cerv_conn_arena_lookup_token(&worker->arena, token);
    struct cerv_mono_time now;
    int64_t unix_seconds;
    enum cerv_conn_result result = CERV_CONN_KEEP;
    if (conn == NULL) return CERV_WORKER_OK;
    if (!cerv_clock_mono_now(&now) || !cerv_clock_unix_now(&unix_seconds)) return CERV_WORKER_FATAL;
    if (conn->state == CERV_CONN_RECV_HEADERS &&
        (events & (uint32_t)(EPOLLIN | EPOLLRDHUP | EPOLLHUP | EPOLLERR)) != 0U) {
        result = cerv_conn_on_readable(conn, worker->root,
                                       worker->config.spa_fallback.len == 0U ? NULL : &worker->config.spa_fallback,
                                       worker->config.immutable, now, unix_seconds,
                                       worker->config.write_timeout);
    } else if (cerv_conn_wants_write(conn) &&
               (events & (uint32_t)(EPOLLOUT | EPOLLRDHUP | EPOLLHUP | EPOLLERR)) != 0U) {
        result = cerv_conn_on_writable(conn, now, worker->config.header_timeout,
                                       worker->config.write_timeout);
    }
    return cerv_worker_handle_conn_result(worker, conn, result);
}

static enum cerv_worker_result cerv_worker_expire(struct cerv_worker *worker)
{
    struct cerv_mono_time now;
    int64_t unix_seconds;
    struct cerv_timer_node node;
    if (!cerv_clock_mono_now(&now) || !cerv_clock_unix_now(&unix_seconds)) return CERV_WORKER_FATAL;
    while (cerv_timer_peek(&worker->timers, &node) && node.deadline_ns <= now.ns) {
        struct cerv_conn *conn;
        enum cerv_conn_result result;
        if (!cerv_timer_pop(&worker->timers, &node)) return CERV_WORKER_FATAL;
        conn = cerv_conn_arena_lookup_token(&worker->arena,
                                            ((uint64_t)node.generation << 32U) | (uint64_t)node.slot_index);
        if (conn == NULL) continue;
        result = cerv_conn_on_deadline(conn, now, unix_seconds, worker->config.write_timeout);
        if (result == CERV_CONN_FATAL) return CERV_WORKER_FATAL;
        if (result == CERV_CONN_CLOSE) {
            if (!cerv_worker_release_conn(worker, conn)) return CERV_WORKER_FATAL;
            continue;
        }
        if (!cerv_worker_epoll_sync(worker, conn) || !cerv_worker_timer_sync(worker, conn)) return CERV_WORKER_FATAL;
    }
    return CERV_WORKER_OK;
}

static int cerv_worker_wait_timeout(const struct cerv_worker *worker, int max_wait_ms, struct cerv_mono_time now)
{
    struct cerv_timer_node node;
    int timeout = max_wait_ms;
    if (cerv_timer_peek(&worker->timers, &node)) {
        uint64_t remaining = cerv_mono_remaining_ms_ceil(now, (struct cerv_mono_time){.ns = node.deadline_ns});
        int timer_timeout = remaining > (uint64_t)INT_MAX ? INT_MAX : (int)remaining;
        if (timeout < 0 || timer_timeout < timeout) timeout = timer_timeout;
    }
    return timeout;
}

enum cerv_worker_result cerv_worker_run_once(struct cerv_worker *worker, int max_wait_ms)
{
    struct epoll_event events[CERV_EPOLL_BATCH_MAX];
    struct cerv_mono_time now;
    enum cerv_worker_result result;
    int timeout;
    int count;
    int i;
    bool control_ready = false;
    if (worker == NULL || worker->epoll_fd < 0 || worker->root == NULL || max_wait_ms < -1) return CERV_WORKER_FATAL;
    result = cerv_worker_expire(worker);
    if (result != CERV_WORKER_OK) return result;
    if (!cerv_clock_mono_now(&now)) return CERV_WORKER_FATAL;
    timeout = cerv_worker_wait_timeout(worker, max_wait_ms, now);
    count = epoll_wait(worker->epoll_fd, events, (int)CERV_EPOLL_BATCH_MAX, timeout);
    if (count < 0) {
        if (errno == EINTR) return CERV_WORKER_OK;
        return CERV_WORKER_FATAL;
    }
    for (i = 0; i < count; ++i) {
        if (events[i].data.u64 == CERV_CONTROL_TOKEN) {
            if ((events[i].events & (uint32_t)(EPOLLERR | EPOLLHUP)) != 0U) return CERV_WORKER_FATAL;
            control_ready = true;
        }
    }
    for (i = 0; i < count; ++i) {
        if (events[i].data.u64 == CERV_CONTROL_TOKEN) continue;
        if (events[i].data.u64 == CERV_LISTENER_TOKEN) {
            if (control_ready || !worker->accepting || !worker->listener_registered) continue;
            if ((events[i].events & (uint32_t)(EPOLLERR | EPOLLHUP)) != 0U) return CERV_WORKER_FATAL;
            result = cerv_worker_accept_ready(worker);
        } else {
            result = cerv_worker_dispatch_conn(worker, events[i].data.u64, events[i].events);
        }
        if (result != CERV_WORKER_OK) return result;
    }
    result = cerv_worker_expire(worker);
    if (result != CERV_WORKER_OK) return result;
    return control_ready ? CERV_WORKER_CONTROL_READY : CERV_WORKER_OK;
}
