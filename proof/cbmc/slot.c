#include "runtime/conn.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

extern unsigned nondet_uint(void);
void __CPROVER_assume(bool condition);

static bool arena_valid(const struct cerv_conn_arena *arena)
{
    bool seen[3] = {false, false, false};
    size_t free_count = 0U;
    uint32_t index = arena->free_head;
    if (arena->capacity != 3U || arena->active > arena->capacity) return false;
    while (index != CERV_SLOT_NONE) {
        const struct cerv_conn *conn;
        if (index >= 3U || seen[index]) return false;
        seen[index] = true;
        conn = &arena->slots[index];
        if (conn->index != index || conn->state != CERV_CONN_FREE || conn->socket_fd != -1 || conn->file_fd != -1) return false;
        ++free_count;
        index = conn->next_free;
    }
    return free_count + arena->active == arena->capacity;
}

int main(void)
{
    struct cerv_conn slots[3];
    struct cerv_conn_arena arena;
    bool held[3] = {false, false, false};
    uint64_t token[3] = {UINT64_C(0), UINT64_C(0), UINT64_C(0)};
    assert(cerv_conn_arena_init(&arena, slots, 3U));
    assert(arena_valid(&arena));
    for (size_t step = 0U; step < 5U; ++step) {
        unsigned op = nondet_uint();
        unsigned slot = nondet_uint();
        __CPROVER_assume(op <= 2U);
        __CPROVER_assume(slot < 3U);
        if (op == 0U) {
            struct cerv_conn *conn = NULL;
            enum cerv_slot_acquire_result result = cerv_conn_arena_acquire(&arena, &conn);
            if (result == CERV_SLOT_ACQUIRE_OK) {
                assert(conn != NULL);
                assert(conn->index < 3U);
                assert(!held[conn->index]);
                held[conn->index] = true;
                conn->state = CERV_CONN_RECV_HEADERS;
                token[conn->index] = cerv_conn_token(conn);
                assert(cerv_conn_arena_lookup_token(&arena, token[conn->index]) == conn);
            } else {
                assert(result == CERV_SLOT_ACQUIRE_FULL);
                assert(arena.active == arena.capacity);
            }
        } else if (op == 1U && held[slot]) {
            uint64_t stale = token[slot];
            slots[slot].state = CERV_CONN_FREE;
            assert(cerv_conn_arena_release(&arena, &slots[slot]));
            held[slot] = false;
            assert(cerv_conn_arena_lookup_token(&arena, stale) == NULL);
        } else if (op == 2U && held[slot]) {
            assert(cerv_conn_arena_lookup_token(&arena, token[slot]) == &slots[slot]);
        }
        assert(arena_valid(&arena));
    }
    return 0;
}
