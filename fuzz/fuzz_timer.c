#include "fuzz_support.h"
#include "runtime/timer.h"

#include <stddef.h>
#include <stdint.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    struct cerv_timer_node storage[8];
    struct cerv_timer_link links[8];
    struct cerv_timer_heap heap;
    size_t i;
    for (i = 0U; i < 8U; ++i) cerv_timer_link_init(&links[i]);
    cerv_fuzz_require(cerv_timer_heap_init(&heap, storage, 8U));
    for (i = 0U; i < size; ++i) {
        size_t slot = (size_t)(data[i] & 7U);
        unsigned op = (unsigned)((data[i] >> 3U) & 3U);
        if (op == 0U || op == 1U) {
            uint64_t deadline = (uint64_t)data[i] * UINT64_C(17) + (uint64_t)i;
            (void)cerv_timer_set(&heap, &links[slot], (uint32_t)slot,
                                 (uint32_t)((i & UINT32_C(0xffff)) + 1U), deadline);
        } else if (op == 2U) {
            cerv_fuzz_require(cerv_timer_remove(&heap, &links[slot]));
        } else {
            struct cerv_timer_node node;
            (void)cerv_timer_pop(&heap, &node);
        }
        cerv_fuzz_require(cerv_timer_heap_valid(&heap));
        cerv_fuzz_require(heap.size <= heap.capacity);
    }
    for (i = 0U; i < 8U; ++i) {
        if (links[i].heap_index != CERV_TIMER_NOT_IN_HEAP) {
            cerv_fuzz_require(links[i].heap_index < heap.size);
            cerv_fuzz_require(heap.nodes[links[i].heap_index].link == &links[i]);
        }
    }
    return 0;
}
