#include "runtime/conn.h"
#include "fuzz_support.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static void check_arena(const struct cerv_conn_arena *arena)
{
    bool seen[8] = {false};
    size_t free_count = 0U;
    size_t index;
    cerv_fuzz_require(arena != NULL && arena->slots != NULL && arena->capacity == 8U);
    cerv_fuzz_require(arena->active <= arena->capacity);
    index = arena->free_head;
    while (index != CERV_SLOT_NONE) {
        const struct cerv_conn *conn;
        cerv_fuzz_require(index < arena->capacity);
        cerv_fuzz_require(!seen[index]);
        seen[index] = true;
        conn = &arena->slots[index];
        cerv_fuzz_require(conn->index == index);
        cerv_fuzz_require(conn->state == CERV_CONN_FREE);
        cerv_fuzz_require(conn->socket_fd == -1 && conn->file_fd == -1);
        ++free_count;
        cerv_fuzz_require(free_count <= arena->capacity);
        index = conn->next_free;
    }
    cerv_fuzz_require(free_count + arena->active == arena->capacity);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    struct cerv_conn slots[8];
    struct cerv_conn_arena arena;
    struct cerv_conn *held[8] = {0};
    uint64_t tokens[8] = {0};
    size_t i;
    cerv_fuzz_require(cerv_conn_arena_init(&arena, slots, 8U));
    check_arena(&arena);
    for (i = 0U; i < size; ++i) {
        unsigned op = (unsigned)(data[i] % UINT8_C(4));
        size_t slot = (size_t)((data[i] >> 2U) % UINT8_C(8));
        if (op == 0U) {
            struct cerv_conn *conn = NULL;
            enum cerv_slot_acquire_result r = cerv_conn_arena_acquire(&arena, &conn);
            if (r == CERV_SLOT_ACQUIRE_OK) {
                cerv_fuzz_require(conn != NULL && conn->index < 8U && held[conn->index] == NULL);
                conn->state = CERV_CONN_RECV_HEADERS;
                held[conn->index] = conn;
                tokens[conn->index] = cerv_conn_token(conn);
                cerv_fuzz_require(cerv_conn_arena_lookup_token(&arena, tokens[conn->index]) == conn);
            } else {
                cerv_fuzz_require(r == CERV_SLOT_ACQUIRE_FULL);
                cerv_fuzz_require(arena.active == arena.capacity);
            }
        } else if (op == 1U && held[slot] != NULL) {
            struct cerv_conn *conn = held[slot];
            uint64_t stale = tokens[slot];
            conn->state = CERV_CONN_FREE;
            cerv_fuzz_require(cerv_conn_arena_release(&arena, conn));
            held[slot] = NULL;
            cerv_fuzz_require(cerv_conn_arena_lookup_token(&arena, stale) == NULL);
        } else if (op == 2U && held[slot] != NULL) {
            cerv_fuzz_require(cerv_conn_arena_lookup_token(&arena, tokens[slot]) == held[slot]);
        } else if (op == 3U) {
            uint64_t mutated = tokens[slot] ^ (UINT64_C(1) << (unsigned)(data[i] & UINT8_C(63)));
            struct cerv_conn *found = cerv_conn_arena_lookup_token(&arena, mutated);
            if (found != NULL) cerv_fuzz_require(found->index < arena.capacity && held[found->index] == found);
        }
        check_arena(&arena);
    }
    for (i = 0U; i < 8U; ++i) {
        if (held[i] != NULL) {
            held[i]->state = CERV_CONN_FREE;
            cerv_fuzz_require(cerv_conn_arena_release(&arena, held[i]));
        }
    }
    check_arena(&arena);
    cerv_fuzz_require(arena.active == 0U);
    return 0;
}
