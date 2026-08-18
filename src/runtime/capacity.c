#include "runtime/capacity.h"

#include "base/bounds.h"
#include "base/checked.h"
#include "runtime/conn.h"
#include "runtime/timer.h"

bool cerv_runtime_worker_memory_bytes(size_t slots, size_t *out)
{
    size_t connections;
    size_t timers;
    if (out == NULL || slots == 0U || !cerv_size_mul(slots, sizeof(struct cerv_conn), &connections) ||
        !cerv_size_mul(slots, sizeof(struct cerv_timer_node), &timers)) return false;
    return cerv_size_add(connections, timers, out);
}

bool cerv_runtime_required_worker_fds(size_t slots, size_t *out)
{
    size_t clients_files;
    if (out == NULL || slots == 0U || !cerv_size_mul(slots, 2U, &clients_files)) return false;
    return cerv_size_add(clients_files, CERV_FD_SAFETY_MARGIN, out);
}

bool cerv_runtime_auto_connections(size_t workers, size_t target, uint64_t nofile_soft,
                                   bool nofile_infinite, size_t *out)
{
    uint64_t usable;
    uint64_t slots_per_worker;
    uint64_t total;
    size_t resolved;
    if (out == NULL || workers == 0U || workers > CERV_WORKERS_MAX || target == 0U || target < workers) return false;
    if (nofile_infinite) {
        *out = target;
        return true;
    }
    if (nofile_soft <= (uint64_t)CERV_FD_SAFETY_MARGIN) return false;
    usable = nofile_soft - (uint64_t)CERV_FD_SAFETY_MARGIN;
    slots_per_worker = usable / UINT64_C(2);
    if (slots_per_worker == UINT64_C(0) || !cerv_u64_mul(slots_per_worker, (uint64_t)workers, &total)) {
        return false;
    }
    if (total >= (uint64_t)target) resolved = target;
    else {
        if (total > (uint64_t)SIZE_MAX) return false;
        resolved = (size_t)total;
    }
    if (resolved < workers) return false;
    *out = resolved;
    return true;
}
