#include "runtime/conn.h"

#include <stddef.h>
#include <stdint.h>

bool cerv_conn_arena_init(struct cerv_conn_arena *arena, struct cerv_conn *slots, size_t capacity)
{
    size_t i;
    if (arena == NULL || (slots == NULL && capacity != 0U) || capacity > (size_t)UINT32_MAX) return false;
    *arena = (struct cerv_conn_arena){.slots = slots, .capacity = capacity, .active = 0U, .free_head = CERV_SLOT_NONE};
    if (capacity == 0U) return true;
    for (i = 0U; i < capacity; ++i) {
        slots[i] = (struct cerv_conn){
            .state = CERV_CONN_FREE,
            .index = (uint32_t)i,
            .generation = 0U,
            .next_free = i + 1U < capacity ? (uint32_t)(i + 1U) : CERV_SLOT_NONE,
            .socket_fd = -1,
            .file_fd = -1
        };
        cerv_timer_link_init(&slots[i].timer);
    }
    arena->free_head = 0U;
    return true;
}

enum cerv_slot_acquire_result cerv_conn_arena_acquire(struct cerv_conn_arena *arena, struct cerv_conn **out)
{
    struct cerv_conn *conn;
    uint32_t index;
    if (out != NULL) *out = NULL;
    if (arena == NULL || out == NULL || (arena->slots == NULL && arena->capacity != 0U)) {
        return CERV_SLOT_ACQUIRE_GENERATION_EXHAUSTED;
    }
    if (arena->free_head == CERV_SLOT_NONE) return CERV_SLOT_ACQUIRE_FULL;
    index = arena->free_head;
    if ((size_t)index >= arena->capacity) return CERV_SLOT_ACQUIRE_GENERATION_EXHAUSTED;
    conn = &arena->slots[index];
    if (conn->state != CERV_CONN_FREE || conn->generation == UINT32_MAX) {
        return CERV_SLOT_ACQUIRE_GENERATION_EXHAUSTED;
    }
    arena->free_head = conn->next_free;
    conn->next_free = CERV_SLOT_NONE;
    ++conn->generation;
    ++arena->active;
    *out = conn;
    return CERV_SLOT_ACQUIRE_OK;
}

bool cerv_conn_arena_release(struct cerv_conn_arena *arena, struct cerv_conn *conn)
{
    uint32_t index;
    if (arena == NULL || conn == NULL || arena->slots == NULL || arena->active == 0U) return false;
    index = conn->index;
    if ((size_t)index >= arena->capacity || &arena->slots[index] != conn || conn->state != CERV_CONN_FREE ||
        conn->socket_fd != -1 || conn->file_fd != -1 || conn->timer.heap_index != CERV_TIMER_NOT_IN_HEAP ||
        conn->next_free != CERV_SLOT_NONE) return false;
    conn->next_free = arena->free_head;
    arena->free_head = index;
    --arena->active;
    return true;
}

uint64_t cerv_conn_token(const struct cerv_conn *conn)
{
    if (conn == NULL) return UINT64_MAX;
    return ((uint64_t)conn->generation << 32U) | (uint64_t)conn->index;
}

struct cerv_conn *cerv_conn_arena_lookup_token(struct cerv_conn_arena *arena, uint64_t token)
{
    uint32_t index;
    uint32_t generation;
    struct cerv_conn *conn;
    if (arena == NULL || arena->slots == NULL) return NULL;
    index = (uint32_t)(token & UINT64_C(0xffffffff));
    generation = (uint32_t)(token >> 32U);
    if ((size_t)index >= arena->capacity) return NULL;
    conn = &arena->slots[index];
    if (conn->state == CERV_CONN_FREE || conn->generation != generation || generation == 0U) return NULL;
    return conn;
}
