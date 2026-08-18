#ifndef CERV_CONN_H
#define CERV_CONN_H

#include "base/bounds.h"
#include "base/cerv_time.h"
#include "linux/fs.h"
#include "serve/response.h"
#include "runtime/timer.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CERV_SLOT_NONE UINT32_MAX

enum cerv_conn_state {
    CERV_CONN_FREE = 0,
    CERV_CONN_RECV_HEADERS,
    CERV_CONN_SEND_HEADERS,
    CERV_CONN_SEND_BODY,
    CERV_CONN_SEND_FILE,
    CERV_CONN_SEND_FALLBACK
};

enum cerv_conn_result {
    CERV_CONN_KEEP = 0,
    CERV_CONN_CLOSE,
    CERV_CONN_FATAL
};

enum cerv_slot_acquire_result {
    CERV_SLOT_ACQUIRE_OK = 0,
    CERV_SLOT_ACQUIRE_FULL,
    CERV_SLOT_ACQUIRE_GENERATION_EXHAUSTED
};

struct cerv_conn {
    enum cerv_conn_state state;
    uint32_t index;
    uint32_t generation;
    uint32_t next_free;
    int socket_fd;
    int file_fd;
    struct cerv_timer_link timer;

    struct cerv_mono_time accepted_at;
    struct cerv_mono_time header_deadline;
    struct cerv_mono_time write_deadline;
    struct cerv_mono_time lifetime_deadline;

    unsigned char request_bytes[CERV_REQUEST_BYTES_MAX];
    size_t request_used;
    unsigned header_delimiter_state;

    struct cerv_response_plan response;
    size_t header_sent;
    size_t body_sent;
    uint64_t file_offset;
    uint64_t file_remaining;
    size_t scratch_len;
    size_t scratch_sent;
    uint32_t requests_completed;
    bool close_after_response;
};

struct cerv_conn_arena {
    struct cerv_conn *slots;
    size_t capacity;
    size_t active;
    uint32_t free_head;
};

bool cerv_conn_arena_init(struct cerv_conn_arena *arena, struct cerv_conn *slots, size_t capacity);
enum cerv_slot_acquire_result cerv_conn_arena_acquire(struct cerv_conn_arena *arena, struct cerv_conn **out);
bool cerv_conn_arena_release(struct cerv_conn_arena *arena, struct cerv_conn *conn);
struct cerv_conn *cerv_conn_arena_lookup_token(struct cerv_conn_arena *arena, uint64_t token);
uint64_t cerv_conn_token(const struct cerv_conn *conn);

bool cerv_conn_begin(struct cerv_conn *conn, int socket_fd, struct cerv_mono_time now,
                     struct cerv_duration header_timeout, struct cerv_duration max_lifetime);
void cerv_conn_cleanup(struct cerv_conn *conn);
bool cerv_conn_wants_read(const struct cerv_conn *conn);
bool cerv_conn_wants_write(const struct cerv_conn *conn);
bool cerv_conn_next_deadline(const struct cerv_conn *conn, struct cerv_mono_time *out);
bool cerv_conn_should_close_response(bool request_close, bool resource_ok, uint32_t requests_completed);

enum cerv_conn_result cerv_conn_on_readable(struct cerv_conn *conn, const struct cerv_fs_root *root,
                                            const struct cerv_path *spa_fallback, bool immutable, struct cerv_mono_time now,
                                            int64_t unix_seconds, struct cerv_duration write_timeout);
enum cerv_conn_result cerv_conn_on_writable(struct cerv_conn *conn, struct cerv_mono_time now,
                                            struct cerv_duration header_timeout,
                                            struct cerv_duration write_timeout);
enum cerv_conn_result cerv_conn_on_deadline(struct cerv_conn *conn, struct cerv_mono_time now,
                                            int64_t unix_seconds, struct cerv_duration write_timeout);

#endif
