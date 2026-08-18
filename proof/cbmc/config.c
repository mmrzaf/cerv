#include "base/bounds.h"
#include "process/config.h"
#include "runtime/conn.h"
#include "runtime/capacity.h"
#include "runtime/timer.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

extern unsigned nondet_uint(void);
void __CPROVER_assume(bool condition);

int main(void)
{
    unsigned total_raw = nondet_uint();
    unsigned workers_raw = nondet_uint();
    unsigned index_raw = nondet_uint();
    size_t total;
    size_t workers;
    size_t index;
    size_t slots = 0U;
    size_t fds = 0U;
    size_t memory = 0U;
    __CPROVER_assume(total_raw >= 1U && total_raw <= 16U);
    __CPROVER_assume(workers_raw >= 1U && workers_raw <= total_raw);
    __CPROVER_assume(index_raw < workers_raw);
    total = (size_t)total_raw;
    workers = (size_t)workers_raw;
    index = (size_t)index_raw;
    assert(cerv_config_partition_slots(total, workers, index, &slots));
    assert(slots >= 1U && slots <= total);
    assert(slots == total / workers + (index < total % workers ? 1U : 0U));
    assert(cerv_runtime_required_worker_fds(slots, &fds));
    assert(fds == slots * 2U + CERV_FD_SAFETY_MARGIN);
    assert(cerv_runtime_worker_memory_bytes(slots, &memory));
    assert(memory == slots * (sizeof(struct cerv_conn) + sizeof(struct cerv_timer_node)));

    {
        unsigned target_raw = nondet_uint();
        unsigned nofile_raw = nondet_uint();
        size_t target;
        size_t resolved = 0U;
        uint64_t nofile;
        bool ok;
        __CPROVER_assume(target_raw >= workers_raw && target_raw <= 16U);
        __CPROVER_assume(nofile_raw <= 64U);
        target = (size_t)target_raw;
        nofile = (uint64_t)nofile_raw;
        ok = cerv_runtime_auto_connections(workers, target, nofile, false, &resolved);
        if (ok) {
            size_t largest = 0U;
            size_t required = 0U;
            assert(resolved >= workers && resolved <= target);
            assert(cerv_config_partition_slots(resolved, workers, 0U, &largest));
            assert(cerv_runtime_required_worker_fds(largest, &required));
            assert((uint64_t)required <= nofile);
        }
        assert(cerv_runtime_auto_connections(workers, target, UINT64_C(0), true, &resolved));
        assert(resolved == target);
    }
    return 0;
}
