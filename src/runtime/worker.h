#ifndef CERV_WORKER_H
#define CERV_WORKER_H

#include "base/cerv_time.h"
#include "http/http_target.h"
#include "runtime/conn.h"
#include "linux/fs.h"
#include "runtime/timer.h"

#include <stdbool.h>
#include <stddef.h>

struct cerv_worker_config {
    struct cerv_duration header_timeout;
    struct cerv_duration write_timeout;
    struct cerv_duration max_lifetime;
    struct cerv_path spa_fallback;
    bool immutable;
    bool shared_listener_cooperative;
};

enum cerv_worker_result {
    CERV_WORKER_OK = 0,
    CERV_WORKER_CONTROL_READY,
    CERV_WORKER_FATAL
};

struct cerv_worker {
    int epoll_fd;
    int listener_fd;
    int control_fd;
    const struct cerv_fs_root *root;
    struct cerv_conn_arena arena;
    struct cerv_timer_heap timers;
    struct cerv_worker_config config;
    bool accepting;
    bool listener_registered;
    bool accept_backoff;
    struct cerv_mono_time accept_resume_at;
};

bool cerv_worker_prepare_process(void);
bool cerv_worker_config_default(struct cerv_worker_config *out);
bool cerv_worker_init(struct cerv_worker *worker, int listener_fd, const struct cerv_fs_root *root,
                      struct cerv_conn *slots, struct cerv_timer_node *timer_storage, size_t slot_count,
                      struct cerv_worker_config config);
void cerv_worker_destroy(struct cerv_worker *worker);
bool cerv_worker_set_control_fd(struct cerv_worker *worker, int control_fd);
bool cerv_worker_stop_accepting(struct cerv_worker *worker);
enum cerv_worker_result cerv_worker_run_once(struct cerv_worker *worker, int max_wait_ms);
size_t cerv_worker_active_connections(const struct cerv_worker *worker);

#endif
