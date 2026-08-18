#include "base/bounds.h"
#include "process/config.h"
#include "process/config_workers.h"
#include "runtime/conn.h"
#include "runtime/capacity.h"
#include "fuzz_support.h"
#include "runtime/timer.h"

#include <stddef.h>
#include <stdint.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    size_t affinity;
    size_t workers;
    size_t total;
    size_t index;
    size_t slots = 0U;
    size_t fds = 0U;
    size_t memory = 0U;
    size_t auto_count = 0U;
    uint64_t quota;
    uint64_t period;
    bool parsed_limited = false;
    uint64_t parsed_quota = UINT64_C(0);
    uint64_t parsed_period = UINT64_C(0);
    if (size != 0U && size < (size_t)96 &&
        cerv_config_parse_cpu_max(data, size, &parsed_limited, &parsed_quota, &parsed_period)) {
        cerv_fuzz_require(parsed_period != UINT64_C(0));
        cerv_fuzz_require(parsed_limited ? parsed_quota != UINT64_C(0) : parsed_quota == UINT64_C(0));
    }
    if (size < 6U) return 0;
    affinity = (size_t)data[0] + 1U;
    quota = (uint64_t)data[1] + UINT64_C(1);
    period = (uint64_t)data[2] + UINT64_C(1);
    cerv_fuzz_require(cerv_config_auto_worker_count(affinity, (data[3] & 1U) != 0U, quota, period, &auto_count));
    cerv_fuzz_require(auto_count >= 1U && auto_count <= affinity && auto_count <= CERV_WORKERS_MAX);

    total = (size_t)(data[4] % 64U) + 1U;
    workers = (size_t)(data[5] % (uint8_t)(total > 255U ? 255U : total)) + 1U;
    index = size > 6U ? (size_t)data[6] % workers : 0U;
    cerv_fuzz_require(cerv_config_partition_slots(total, workers, index, &slots));
    cerv_fuzz_require(slots == total / workers + (index < total % workers ? 1U : 0U));
    cerv_fuzz_require(cerv_runtime_required_worker_fds(slots, &fds));
    cerv_fuzz_require(fds == slots * 2U + CERV_FD_SAFETY_MARGIN);
    cerv_fuzz_require(cerv_runtime_worker_memory_bytes(slots, &memory));
    cerv_fuzz_require(memory == slots * (sizeof(struct cerv_conn) + sizeof(struct cerv_timer_node)));
    {
        size_t target = (size_t)(data[4] % 64U) + workers;
        size_t auto_connections = 0U;
        uint64_t nofile = (uint64_t)data[5] + (uint64_t)CERV_FD_SAFETY_MARGIN + UINT64_C(2);
        if (cerv_runtime_auto_connections(workers, target, nofile, false, &auto_connections)) {
            cerv_fuzz_require(auto_connections >= workers && auto_connections <= target);
        }
    }
    return 0;
}
