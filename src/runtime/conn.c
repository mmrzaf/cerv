#define _POSIX_C_SOURCE 200809L
#include "runtime/conn.h"

#include "base/checked.h"
#include "http/http_request.h"
#include "http/http_target.h"
#include "serve/representation.h"

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <unistd.h>

static void cerv_conn_reset_runtime(struct cerv_conn *conn)
{
    uint32_t index = conn->index;
    uint32_t generation = conn->generation;
    uint32_t next_free = conn->next_free;
    *conn = (struct cerv_conn){
        .state = CERV_CONN_FREE,
        .index = index,
        .generation = generation,
        .next_free = next_free,
        .socket_fd = -1,
        .file_fd = -1
    };
    cerv_timer_link_init(&conn->timer);
}

bool cerv_conn_begin(struct cerv_conn *conn, int socket_fd, struct cerv_mono_time now,
                     struct cerv_duration header_timeout, struct cerv_duration max_lifetime)
{
    uint32_t index;
    uint32_t generation;
    if (conn == NULL || conn->state != CERV_CONN_FREE || socket_fd < 0 || conn->generation == 0U ||
        header_timeout.ns == 0U || max_lifetime.ns == 0U) return false;
    index = conn->index;
    generation = conn->generation;
    *conn = (struct cerv_conn){
        .state = CERV_CONN_RECV_HEADERS,
        .index = index,
        .generation = generation,
        .next_free = CERV_SLOT_NONE,
        .socket_fd = socket_fd,
        .file_fd = -1,
        .accepted_at = now,
        .header_deadline = cerv_mono_add_saturating(now, header_timeout),
        .write_deadline = {.ns = UINT64_MAX},
        .lifetime_deadline = cerv_mono_add_saturating(now, max_lifetime)
    };
    cerv_timer_link_init(&conn->timer);
    return true;
}

void cerv_conn_cleanup(struct cerv_conn *conn)
{
    if (conn == NULL) return;
    if (conn->socket_fd >= 0) (void)close(conn->socket_fd);
    if (conn->file_fd >= 0) (void)close(conn->file_fd);
    cerv_conn_reset_runtime(conn);
}

static bool cerv_conn_request_line_is_head(const struct cerv_conn *conn)
{
    size_t i;
    struct cerv_http_request req;
    if (conn == NULL) return false;
    for (i = 0U; i + 1U < conn->request_used && i <= CERV_REQUEST_LINE_MAX; ++i) {
        if (conn->request_bytes[i] == (unsigned char)'\r' && conn->request_bytes[i + 1U] == (unsigned char)'\n') {
            enum cerv_parse_result result = cerv_http_request_line_parse(
                (struct cerv_span){.ptr = conn->request_bytes, .len = i}, &req);
            return result != CERV_PARSE_INCOMPLETE && req.method == CERV_METHOD_HEAD;
        }
    }
    return false;
}

static void cerv_conn_note_write_progress(struct cerv_conn *conn, struct cerv_mono_time now,
                                          struct cerv_duration write_timeout)
{
    conn->write_deadline = cerv_mono_add_saturating(now, write_timeout);
}

static enum cerv_conn_result cerv_conn_install_error(struct cerv_conn *conn, enum cerv_http_status status,
                                                     bool head_request, int64_t unix_seconds,
                                                     struct cerv_mono_time now,
                                                     struct cerv_duration write_timeout)
{
    if (!cerv_response_plan_error(status, head_request, unix_seconds, &conn->response)) return CERV_CONN_FATAL;
    conn->header_sent = 0U;
    conn->body_sent = 0U;
    conn->file_offset = UINT64_C(0);
    conn->file_remaining = UINT64_C(0);
    conn->scratch_len = 0U;
    conn->scratch_sent = 0U;
    conn->close_after_response = true;
    conn->state = CERV_CONN_SEND_HEADERS;
    conn->write_deadline = cerv_mono_add_saturating(now, write_timeout);
    return CERV_CONN_KEEP;
}

static enum cerv_conn_result cerv_conn_install_resource(struct cerv_conn *conn,
                                                        const struct cerv_http_request *request,
                                                        const struct cerv_fs_root *root,
                                                        const struct cerv_path *spa_fallback, bool immutable,
                                                        int64_t unix_seconds, struct cerv_mono_time now,
                                                        struct cerv_duration write_timeout)
{
    struct cerv_path path;
    struct cerv_representation rep = {.file = {.fd = -1}};
    enum cerv_target_result target_result;
    enum cerv_representation_result rep_result;
    bool close_connection;
    bool ok;

    target_result = cerv_http_target_decode_path(&request->target, &path);
    if (target_result == CERV_TARGET_TOO_LONG) {
        return cerv_conn_install_error(conn, CERV_STATUS_414, request->method == CERV_METHOD_HEAD,
                                       unix_seconds, now, write_timeout);
    }
    if (target_result != CERV_TARGET_OK) {
        return cerv_conn_install_error(conn, CERV_STATUS_400, request->method == CERV_METHOD_HEAD,
                                       unix_seconds, now, write_timeout);
    }
    rep_result = cerv_representation_select(root, &path, &request->accept_encoding, &rep);
    if (rep_result == CERV_REPRESENTATION_NOT_FOUND && spa_fallback != NULL && spa_fallback->len != 0U &&
        !(path.len == spa_fallback->len && memcmp(path.bytes, spa_fallback->bytes, path.len) == 0)) {
        cerv_representation_close(&rep);
        rep_result = cerv_representation_select(root, spa_fallback, &request->accept_encoding, &rep);
    }
    if (rep_result == CERV_REPRESENTATION_UNSUPPORTED) {
        cerv_representation_close(&rep);
        return CERV_CONN_FATAL;
    }
    close_connection = conn->close_after_response ||
                       cerv_conn_should_close_response(request->connection_close,
                                                       rep_result == CERV_REPRESENTATION_OK,
                                                       conn->requests_completed);
    ok = cerv_response_plan_resource_connection(request, rep_result,
                                                rep_result == CERV_REPRESENTATION_OK ? &rep : NULL,
                                                unix_seconds, immutable, close_connection, &conn->response);
    if (!ok) {
        cerv_representation_close(&rep);
        return CERV_CONN_FATAL;
    }
    conn->header_sent = 0U;
    conn->body_sent = 0U;
    conn->scratch_len = 0U;
    conn->scratch_sent = 0U;
    conn->file_remaining = UINT64_C(0);
    conn->file_offset = UINT64_C(0);
    if (conn->response.send_file) {
        int fd = cerv_representation_take_fd(&rep);
        if (fd < 0 || conn->response.file_offset < (off_t)0) {
            cerv_representation_close(&rep);
            if (fd >= 0) (void)close(fd);
            return CERV_CONN_FATAL;
        }
        conn->file_fd = fd;
        conn->file_offset = (uint64_t)conn->response.file_offset;
        conn->file_remaining = conn->response.file_count;
    }
    cerv_representation_close(&rep);
    conn->close_after_response = close_connection;
    conn->state = CERV_CONN_SEND_HEADERS;
    conn->write_deadline = cerv_mono_add_saturating(now, write_timeout);
    return CERV_CONN_KEEP;
}

static enum cerv_conn_result cerv_conn_finish_headers(struct cerv_conn *conn, const struct cerv_fs_root *root,
                                                      const struct cerv_path *spa_fallback, bool immutable,
                                                      struct cerv_mono_time now,
                                                      int64_t unix_seconds,
                                                      struct cerv_duration write_timeout)
{
    struct cerv_http_request request;
    enum cerv_parse_result parsed = cerv_http_request_parse(conn->request_bytes, conn->request_used, &request);
    if (parsed == CERV_PARSE_INCOMPLETE || parsed == CERV_PARSE_OK) {
        if (parsed == CERV_PARSE_INCOMPLETE) return CERV_CONN_FATAL;
        return cerv_conn_install_resource(conn, &request, root, spa_fallback, immutable,
                                          unix_seconds, now, write_timeout);
    }
    return cerv_conn_install_error(conn, cerv_response_status_from_parse(parsed),
                                   cerv_conn_request_line_is_head(conn), unix_seconds, now, write_timeout);
}

static void cerv_conn_scan_delimiter(struct cerv_conn *conn, const unsigned char *data, size_t len)
{
    size_t i;
    for (i = 0U; i < len && conn->header_delimiter_state < 4U; ++i) {
        unsigned char c = data[i];
        switch (conn->header_delimiter_state) {
        case 0U:
            conn->header_delimiter_state = c == (unsigned char)'\r' ? 1U : 0U;
            break;
        case 1U:
            if (c == (unsigned char)'\n') conn->header_delimiter_state = 2U;
            else conn->header_delimiter_state = c == (unsigned char)'\r' ? 1U : 0U;
            break;
        case 2U:
            conn->header_delimiter_state = c == (unsigned char)'\r' ? 3U : 0U;
            break;
        case 3U:
            if (c == (unsigned char)'\n') conn->header_delimiter_state = 4U;
            else conn->header_delimiter_state = c == (unsigned char)'\r' ? 1U : 0U;
            break;
        case 4U:
        default:
            break;
        }
    }
}

enum cerv_conn_result cerv_conn_on_readable(struct cerv_conn *conn, const struct cerv_fs_root *root,
                                            const struct cerv_path *spa_fallback, bool immutable,
                                            struct cerv_mono_time now,
                                            int64_t unix_seconds, struct cerv_duration write_timeout)
{
    size_t operations = 0U;
    if (conn == NULL || root == NULL || conn->state != CERV_CONN_RECV_HEADERS || conn->socket_fd < 0 ||
        write_timeout.ns == 0U) return CERV_CONN_FATAL;
    while (operations < CERV_SOCKET_IO_QUANTUM) {
        size_t available;
        ssize_t received;
        size_t old_used;
        if (conn->request_used >= CERV_REQUEST_BYTES_MAX) {
            return cerv_conn_install_error(conn, CERV_STATUS_431, cerv_conn_request_line_is_head(conn),
                                           unix_seconds, now, write_timeout);
        }
        available = CERV_REQUEST_BYTES_MAX - conn->request_used;
        old_used = conn->request_used;
        ++operations;
        received = recv(conn->socket_fd, conn->request_bytes + old_used, available, 0);
        if (received > 0) {
            size_t count = (size_t)received;
            conn->request_used += count;
            cerv_conn_scan_delimiter(conn, conn->request_bytes + old_used, count);
            if (conn->header_delimiter_state == 4U) {
                return cerv_conn_finish_headers(conn, root, spa_fallback, immutable, now, unix_seconds, write_timeout);
            }
            if (conn->request_used == CERV_REQUEST_BYTES_MAX) {
                return cerv_conn_install_error(conn, CERV_STATUS_431, cerv_conn_request_line_is_head(conn),
                                               unix_seconds, now, write_timeout);
            }
            continue;
        }
        if (received == 0) return CERV_CONN_CLOSE;
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return CERV_CONN_KEEP;
        return CERV_CONN_CLOSE;
    }
    return CERV_CONN_KEEP;
}

static enum cerv_conn_result cerv_conn_finish_response(struct cerv_conn *conn, struct cerv_mono_time now,
                                                        struct cerv_duration header_timeout)
{
    if (conn == NULL || header_timeout.ns == 0U) return CERV_CONN_FATAL;
    if (conn->file_fd >= 0) {
        (void)close(conn->file_fd);
        conn->file_fd = -1;
    }
    if (conn->close_after_response) return CERV_CONN_CLOSE;
    if (conn->requests_completed >= CERV_KEEPALIVE_REQUESTS_MAX) return CERV_CONN_FATAL;
    ++conn->requests_completed;
    conn->request_used = 0U;
    conn->header_delimiter_state = 0U;
    conn->response = (struct cerv_response_plan){0};
    conn->header_sent = 0U;
    conn->body_sent = 0U;
    conn->file_offset = UINT64_C(0);
    conn->file_remaining = UINT64_C(0);
    conn->scratch_len = 0U;
    conn->scratch_sent = 0U;
    conn->close_after_response = false;
    conn->header_deadline = cerv_mono_add_saturating(now, header_timeout);
    conn->write_deadline = (struct cerv_mono_time){.ns = UINT64_MAX};
    conn->state = CERV_CONN_RECV_HEADERS;
    return CERV_CONN_KEEP;
}

static enum cerv_conn_result cerv_conn_send_bytes(struct cerv_conn *conn, const unsigned char *bytes,
                                                  size_t length, size_t *sent,
                                                  struct cerv_mono_time now,
                                                  struct cerv_duration write_timeout)
{
    ssize_t n;
    if (*sent >= length) return CERV_CONN_KEEP;
    n = send(conn->socket_fd, bytes + *sent, length - *sent, MSG_NOSIGNAL);
    if (n > 0) {
        *sent += (size_t)n;
        cerv_conn_note_write_progress(conn, now, write_timeout);
        return CERV_CONN_KEEP;
    }
    if (n == 0) return CERV_CONN_CLOSE;
    if (errno == EINTR) return CERV_CONN_KEEP;
    if (errno == EAGAIN || errno == EWOULDBLOCK) return CERV_CONN_KEEP;
    return CERV_CONN_CLOSE;
}

static enum cerv_conn_result cerv_conn_send_file(struct cerv_conn *conn, struct cerv_mono_time now,
                                                 struct cerv_duration write_timeout,
                                                 size_t *operations, size_t *file_budget)
{
    off_t offset;
    size_t want;
    ssize_t n;
    uint64_t next;
    if (conn->file_remaining == UINT64_C(0)) return CERV_CONN_CLOSE;
    if (*file_budget == 0U || *operations >= CERV_SOCKET_IO_QUANTUM) return CERV_CONN_KEEP;
    want = conn->file_remaining < (uint64_t)*file_budget ? (size_t)conn->file_remaining : *file_budget;
    if (!cerv_u64_to_off_t(conn->file_offset, &offset)) return CERV_CONN_FATAL;
    ++*operations;
    n = sendfile(conn->socket_fd, conn->file_fd, &offset, want);
    if (n > 0) {
        size_t sent = (size_t)n;
        if ((uint64_t)sent > conn->file_remaining || sent > *file_budget ||
            !cerv_u64_add(conn->file_offset, (uint64_t)sent, &next)) return CERV_CONN_FATAL;
        conn->file_offset = next;
        conn->file_remaining -= (uint64_t)sent;
        *file_budget -= sent;
        cerv_conn_note_write_progress(conn, now, write_timeout);
        return CERV_CONN_KEEP;
    }
    if (n == 0) return CERV_CONN_CLOSE;
    if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) return CERV_CONN_KEEP;
    if (errno == EINVAL || errno == ENOSYS) {
        conn->state = CERV_CONN_SEND_FALLBACK;
        conn->scratch_len = 0U;
        conn->scratch_sent = 0U;
        return CERV_CONN_KEEP;
    }
    return CERV_CONN_CLOSE;
}

static enum cerv_conn_result cerv_conn_send_fallback(struct cerv_conn *conn, struct cerv_mono_time now,
                                                     struct cerv_duration write_timeout,
                                                     size_t *operations, size_t *file_budget)
{
    while (*operations < CERV_SOCKET_IO_QUANTUM && *file_budget > 0U) {
        if (conn->file_remaining == UINT64_C(0)) return CERV_CONN_CLOSE;
        if (conn->scratch_sent == conn->scratch_len) {
            off_t offset;
            size_t want = CERV_REQUEST_BYTES_MAX;
            ssize_t n;
            if ((uint64_t)want > conn->file_remaining) want = (size_t)conn->file_remaining;
            if (want > *file_budget) want = *file_budget;
            if (!cerv_u64_to_off_t(conn->file_offset, &offset)) return CERV_CONN_FATAL;
            ++*operations;
            n = pread(conn->file_fd, conn->request_bytes, want, offset);
            if (n > 0) {
                conn->scratch_len = (size_t)n;
                conn->scratch_sent = 0U;
            } else if (n == 0) {
                return CERV_CONN_CLOSE;
            } else if (errno == EINTR) {
                continue;
            } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return CERV_CONN_KEEP;
            } else {
                return CERV_CONN_CLOSE;
            }
        }
        if (conn->scratch_sent < conn->scratch_len && *operations < CERV_SOCKET_IO_QUANTUM) {
            size_t available = conn->scratch_len - conn->scratch_sent;
            size_t want = available < *file_budget ? available : *file_budget;
            ssize_t n;
            uint64_t next;
            ++*operations;
            n = send(conn->socket_fd, conn->request_bytes + conn->scratch_sent, want, MSG_NOSIGNAL);
            if (n > 0) {
                size_t sent = (size_t)n;
                if ((uint64_t)sent > conn->file_remaining || sent > *file_budget ||
                    !cerv_u64_add(conn->file_offset, (uint64_t)sent, &next)) return CERV_CONN_FATAL;
                conn->scratch_sent += sent;
                conn->file_offset = next;
                conn->file_remaining -= (uint64_t)sent;
                *file_budget -= sent;
                cerv_conn_note_write_progress(conn, now, write_timeout);
                if (conn->scratch_sent == conn->scratch_len) {
                    conn->scratch_len = 0U;
                    conn->scratch_sent = 0U;
                }
                if (conn->file_remaining == UINT64_C(0)) return CERV_CONN_KEEP;
                continue;
            }
            if (n == 0) return CERV_CONN_CLOSE;
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) return CERV_CONN_KEEP;
            return CERV_CONN_CLOSE;
        }
    }
    return CERV_CONN_KEEP;
}

enum cerv_conn_result cerv_conn_on_writable(struct cerv_conn *conn, struct cerv_mono_time now,
                                            struct cerv_duration header_timeout,
                                            struct cerv_duration write_timeout)
{
    size_t operations = 0U;
    size_t file_budget = CERV_FILE_SEND_QUANTUM;
    if (conn == NULL || !cerv_conn_wants_write(conn) || conn->socket_fd < 0 ||
        header_timeout.ns == 0U || write_timeout.ns == 0U) {
        return CERV_CONN_FATAL;
    }
    while (operations < CERV_SOCKET_IO_QUANTUM) {
        enum cerv_conn_result result;
        if (conn->state == CERV_CONN_SEND_HEADERS) {
            size_t before = conn->header_sent;
            ++operations;
            result = cerv_conn_send_bytes(conn, conn->response.headers, conn->response.header_len,
                                          &conn->header_sent, now, write_timeout);
            if (result != CERV_CONN_KEEP) return result;
            if (conn->header_sent < conn->response.header_len) {
                if (conn->header_sent == before) return CERV_CONN_KEEP;
                continue;
            }
            if (conn->response.send_body && conn->response.body_len != 0U) conn->state = CERV_CONN_SEND_BODY;
            else if (conn->response.send_file && conn->file_remaining != UINT64_C(0)) conn->state = CERV_CONN_SEND_FILE;
            else return cerv_conn_finish_response(conn, now, header_timeout);
            continue;
        }
        if (conn->state == CERV_CONN_SEND_BODY) {
            size_t before = conn->body_sent;
            ++operations;
            result = cerv_conn_send_bytes(conn, conn->response.body, conn->response.body_len,
                                          &conn->body_sent, now, write_timeout);
            if (result != CERV_CONN_KEEP) return result;
            if (conn->body_sent >= conn->response.body_len) return cerv_conn_finish_response(conn, now, header_timeout);
            if (conn->body_sent == before) return CERV_CONN_KEEP;
            continue;
        }
        if (conn->state == CERV_CONN_SEND_FILE) {
            result = cerv_conn_send_file(conn, now, write_timeout, &operations, &file_budget);
            if (result != CERV_CONN_KEEP) return result;
            if (conn->state == CERV_CONN_SEND_FALLBACK) continue;
            if (conn->file_remaining == UINT64_C(0)) {
                return cerv_conn_finish_response(conn, now, header_timeout);
            }
            if (file_budget == 0U) return CERV_CONN_KEEP;
            continue;
        }
        if (conn->state == CERV_CONN_SEND_FALLBACK) {
            result = cerv_conn_send_fallback(conn, now, write_timeout, &operations, &file_budget);
            if (result != CERV_CONN_KEEP) return result;
            if (conn->file_remaining == UINT64_C(0)) {
                return cerv_conn_finish_response(conn, now, header_timeout);
            }
            return CERV_CONN_KEEP;
        }
        return CERV_CONN_FATAL;
    }
    return CERV_CONN_KEEP;
}

enum cerv_conn_result cerv_conn_on_deadline(struct cerv_conn *conn, struct cerv_mono_time now,
                                            int64_t unix_seconds, struct cerv_duration write_timeout)
{
    if (conn == NULL || conn->state == CERV_CONN_FREE || write_timeout.ns == 0U) return CERV_CONN_FATAL;
    if (now.ns >= conn->lifetime_deadline.ns) return CERV_CONN_CLOSE;
    if (conn->state == CERV_CONN_RECV_HEADERS && now.ns >= conn->header_deadline.ns) {
        return cerv_conn_install_error(conn, CERV_STATUS_408, cerv_conn_request_line_is_head(conn),
                                       unix_seconds, now, write_timeout);
    }
    if (cerv_conn_wants_write(conn) && now.ns >= conn->write_deadline.ns) return CERV_CONN_CLOSE;
    return CERV_CONN_KEEP;
}
