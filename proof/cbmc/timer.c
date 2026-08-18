#include "runtime/timer.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

extern unsigned nondet_uint(void);
extern uint64_t nondet_u64(void);
void __CPROVER_assume(bool condition);

int main(void)
{
    struct cerv_timer_node storage[3];
    struct cerv_timer_link links[3];
    struct cerv_timer_heap heap;
    for (size_t i = 0U; i < 3U; ++i) cerv_timer_link_init(&links[i]);
    assert(cerv_timer_heap_init(&heap, storage, 3U));
    for (size_t step = 0U; step < 4U; ++step) {
        unsigned op = nondet_uint();
        unsigned slot = nondet_uint();
        uint64_t deadline = nondet_u64();
        __CPROVER_assume(op <= 2U);
        __CPROVER_assume(slot < 3U);
        __CPROVER_assume(deadline <= UINT64_C(15));
        if (op == 0U) {
            (void)cerv_timer_set(&heap, &links[slot], slot, 1U, deadline);
        } else if (op == 1U) {
            assert(cerv_timer_remove(&heap, &links[slot]));
        } else {
            struct cerv_timer_node node;
            (void)cerv_timer_pop(&heap, &node);
        }
        assert(heap.size <= heap.capacity);
        assert(cerv_timer_heap_valid(&heap));
    }
    return 0;
}
