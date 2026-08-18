#ifndef CERV_TIMER_H
#define CERV_TIMER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CERV_TIMER_NOT_IN_HEAP SIZE_MAX

struct cerv_timer_link {
    size_t heap_index;
};

struct cerv_timer_node {
    struct cerv_timer_link *link;
    uint64_t deadline_ns;
    uint32_t slot_index;
    uint32_t generation;
};

struct cerv_timer_heap {
    struct cerv_timer_node *nodes;
    size_t size;
    size_t capacity;
};

void cerv_timer_link_init(struct cerv_timer_link *link);
bool cerv_timer_heap_init(struct cerv_timer_heap *heap, struct cerv_timer_node *storage, size_t capacity);
bool cerv_timer_set(struct cerv_timer_heap *heap, struct cerv_timer_link *link, uint32_t slot_index,
                    uint32_t generation, uint64_t deadline_ns);
bool cerv_timer_remove(struct cerv_timer_heap *heap, struct cerv_timer_link *link);
bool cerv_timer_peek(const struct cerv_timer_heap *heap, struct cerv_timer_node *out);
bool cerv_timer_pop(struct cerv_timer_heap *heap, struct cerv_timer_node *out);
bool cerv_timer_heap_valid(const struct cerv_timer_heap *heap);

#endif
