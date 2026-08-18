#include "base/bounds.h"
#include "base/buffer.h"
#include "base/checked.h"
#include "http/http_fields.h"
#include "http/http_range.h"
#include "http/http_target.h"
#include "runtime/conn.h"
#include "runtime/capacity.h"
#include "process/config.h"
#include "process/config_workers.h"
#include "runtime/timer.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static uint64_t checks = UINT64_C(0);
static uint64_t failures = UINT64_C(0);

#define MODEL_CHECK(expr) do { \
    ++checks; \
    if (!(expr)) { \
        ++failures; \
        fprintf(stderr, "MODEL FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
    } \
} while (0)


static void check_checked_arithmetic(void)
{
    for (uint64_t a = UINT64_C(0); a <= UINT64_C(255); ++a) {
        for (uint64_t b = UINT64_C(0); b <= UINT64_C(255); ++b) {
            uint64_t out = UINT64_C(0);
            MODEL_CHECK(cerv_u64_add(a, b, &out) && out == a + b);
            MODEL_CHECK(cerv_u64_mul(a, b, &out) && out == a * b);
            if (a >= b) {
                MODEL_CHECK(cerv_u64_sub(a, b, &out) && out == a - b);
            } else {
                MODEL_CHECK(!cerv_u64_sub(a, b, &out));
            }
        }
    }
    {
        uint64_t out = UINT64_C(0);
        MODEL_CHECK(!cerv_u64_add(UINT64_MAX, UINT64_C(1), &out));
        MODEL_CHECK(!cerv_u64_mul(UINT64_MAX, UINT64_C(2), &out));
    }
}

static void check_decimal(void)
{
    unsigned char digits[4];
    for (unsigned value = 0U; value <= 9999U; ++value) {
        unsigned n = value;
        size_t len = value < 10U ? 1U : value < 100U ? 2U : value < 1000U ? 3U : 4U;
        uint64_t parsed = UINT64_C(0);
        for (size_t i = 0U; i < len; ++i) {
            size_t pos = len - 1U - i;
            digits[pos] = (unsigned char)('0' + (n % 10U));
            n /= 10U;
        }
        MODEL_CHECK(cerv_u64_decimal(digits, len, &parsed));
        MODEL_CHECK(parsed == (uint64_t)value);
    }
}

static bool reference_qvalue(const unsigned char *p, size_t len, uint16_t *out)
{
    unsigned value = 0U;
    size_t frac = 0U;
    if (len == 0U || (p[0] != (unsigned char)'0' && p[0] != (unsigned char)'1')) return false;
    value = p[0] == (unsigned char)'1' ? 1000U : 0U;
    if (len == 1U) {
        *out = (uint16_t)value;
        return true;
    }
    if (p[1] != (unsigned char)'.' || len > 5U) return false;
    frac = len - 2U;
    for (size_t i = 0U; i < frac; ++i) {
        if (p[2U + i] < (unsigned char)'0' || p[2U + i] > (unsigned char)'9') return false;
        if (p[0] == (unsigned char)'1' && p[2U + i] != (unsigned char)'0') return false;
        if (p[0] == (unsigned char)'0') {
            unsigned digit = (unsigned)(p[2U + i] - (unsigned char)'0');
            value += i == 0U ? digit * 100U : i == 1U ? digit * 10U : digit;
        }
    }
    *out = (uint16_t)value;
    return true;
}

static void check_qvalues(void)
{
    static const unsigned char alphabet[] = {'0','1','.','2','5','9','x',' '};
    unsigned char bytes[5];
    const size_t base = sizeof(alphabet) / sizeof(alphabet[0]);
    uint64_t variants = UINT64_C(1);

    for (size_t len = 1U; len <= sizeof(bytes); ++len) {
        variants *= (uint64_t)base;
        for (uint64_t code = UINT64_C(0); code < variants; ++code) {
            uint64_t x = code;
            uint16_t expected = 0U;
            uint16_t actual = 0U;
            bool expected_ok;
            enum cerv_q_parse_result actual_result;
            for (size_t i = 0U; i < len; ++i) {
                bytes[i] = alphabet[x % (uint64_t)base];
                x /= (uint64_t)base;
            }
            expected_ok = reference_qvalue(bytes, len, &expected);
            actual_result = cerv_http_qvalue_parse((struct cerv_span){.ptr = bytes, .len = len}, &actual);
            MODEL_CHECK((actual_result == CERV_Q_OK) == expected_ok);
            if (expected_ok) MODEL_CHECK(actual == expected && actual <= 1000U);
        }
    }
}

static void check_ranges(void)
{
    for (unsigned kind = 0U; kind <= 2U; ++kind) {
        for (uint64_t first = UINT64_C(0); first <= UINT64_C(20); ++first) {
            for (uint64_t second = UINT64_C(0); second <= UINT64_C(20); ++second) {
                for (uint64_t length = UINT64_C(0); length <= UINT64_C(16); ++length) {
                    struct cerv_range_spec spec = {
                        .kind = (enum cerv_range_kind)kind,
                        .first = first,
                        .second = second
                    };
                    struct cerv_range_selection selected;
                    enum cerv_range_normalize_result result = cerv_http_range_normalize(spec, length, &selected);
                    if (result == CERV_RANGE_SATISFIABLE) {
                        MODEL_CHECK(length != UINT64_C(0));
                        MODEL_CHECK(selected.start <= selected.end);
                        MODEL_CHECK(selected.end < length);
                        MODEL_CHECK(selected.count == selected.end - selected.start + UINT64_C(1));
                    }
                }
            }
        }
    }
}

static void check_buffers(void)
{
    unsigned char storage[16];
    unsigned char source[20];
    memset(source, 0x5a, sizeof(source));
    for (size_t capacity = 0U; capacity <= sizeof(storage); ++capacity) {
        for (size_t len = 0U; len <= sizeof(source); ++len) {
            struct cerv_buffer buffer;
            memset(storage, 0xa5, sizeof(storage));
            cerv_buffer_init(&buffer, storage, capacity);
            MODEL_CHECK(cerv_buffer_append(&buffer, source, len) == (len <= capacity));
            MODEL_CHECK(buffer.used == (len <= capacity ? len : 0U));
            MODEL_CHECK(buffer.used <= buffer.capacity);
        }
    }
    for (size_t used = 0U; used <= sizeof(storage); ++used) {
        for (size_t source_pos = 0U; source_pos <= sizeof(storage); ++source_pos) {
            const size_t source_room = sizeof(storage) - source_pos;
            for (size_t len = 0U; len <= source_room; ++len) {
                struct cerv_buffer buffer;
                unsigned char expected[sizeof(storage)];
                const bool fits = len <= sizeof(storage) - used;
                for (size_t i = 0U; i < sizeof(storage); ++i) storage[i] = (unsigned char)i;
                memcpy(expected, storage, sizeof(storage));
                cerv_buffer_init(&buffer, storage, sizeof(storage));
                buffer.used = used;
                if (fits) memmove(expected + used, expected + source_pos, len);
                MODEL_CHECK(cerv_buffer_append(&buffer, storage + source_pos, len) == fits);
                MODEL_CHECK(buffer.used == (fits ? used + len : used));
                MODEL_CHECK(buffer.used <= buffer.capacity);
                MODEL_CHECK(memcmp(storage, expected, sizeof(storage)) == 0);
            }
        }
    }
}

static void check_paths(void)
{
    static const unsigned char alphabet[] = {'a','/','.','%','2','e','F','5','c','?'};
    unsigned char raw[6];
    const size_t base = sizeof(alphabet) / sizeof(alphabet[0]);
    uint64_t variants = UINT64_C(1);
    raw[0] = (unsigned char)'/';

    for (size_t len = 1U; len <= sizeof(raw); ++len) {
        if (len > 1U) variants *= (uint64_t)base;
        for (uint64_t code = UINT64_C(0); code < variants; ++code) {
            uint64_t x = code;
            struct cerv_http_target target;
            enum cerv_target_result parsed;
            for (size_t i = 1U; i < len; ++i) {
                raw[i] = alphabet[x % (uint64_t)base];
                x /= (uint64_t)base;
            }
            parsed = cerv_http_target_parse((struct cerv_span){.ptr = raw, .len = len}, &target);
            if (parsed == CERV_TARGET_OK) {
                struct cerv_path path;
                enum cerv_target_result decoded = cerv_http_target_decode_path(&target, &path);
                if (decoded == CERV_TARGET_OK) {
                    MODEL_CHECK(path.len < CERV_PATH_BYTES_MAX);
                    MODEL_CHECK(path.bytes[path.len] == 0U);
                    MODEL_CHECK(memchr(path.bytes, 0, path.len) == NULL);
                }
            }
        }
    }
}


static void check_timer_heap(void)
{
    const unsigned base = 7U;
    const unsigned depth = 5U;
    uint64_t variants = UINT64_C(1);
    unsigned d;
    for (d = 0U; d < depth; ++d) variants *= (uint64_t)base;
    for (uint64_t code = UINT64_C(0); code < variants; ++code) {
        struct cerv_timer_node storage[3];
        struct cerv_timer_link links[3];
        struct cerv_timer_heap heap;
        uint64_t x = code;
        size_t i;
        for (i = 0U; i < 3U; ++i) cerv_timer_link_init(&links[i]);
        MODEL_CHECK(cerv_timer_heap_init(&heap, storage, 3U));
        for (i = 0U; i < depth; ++i) {
            unsigned op = (unsigned)(x % (uint64_t)base);
            x /= (uint64_t)base;
            if (op < 3U) {
                (void)cerv_timer_set(&heap, &links[op], op, 1U, (uint64_t)(depth - i) * UINT64_C(10) + (uint64_t)op);
            } else if (op < 6U) {
                MODEL_CHECK(cerv_timer_remove(&heap, &links[op - 3U]));
            } else {
                struct cerv_timer_node node;
                (void)cerv_timer_pop(&heap, &node);
            }
            MODEL_CHECK(cerv_timer_heap_valid(&heap));
            MODEL_CHECK(heap.size <= heap.capacity);
        }
        {
            struct cerv_timer_node previous;
            bool have_previous = false;
            struct cerv_timer_node node;
            while (cerv_timer_pop(&heap, &node)) {
                if (have_previous) {
                    MODEL_CHECK(previous.deadline_ns < node.deadline_ns ||
                                (previous.deadline_ns == node.deadline_ns && previous.slot_index <= node.slot_index));
                }
                previous = node;
                have_previous = true;
            }
            MODEL_CHECK(cerv_timer_heap_valid(&heap));
        }
    }
}

static void check_slot_generation(void)
{
    struct cerv_conn slots[3];
    struct cerv_conn_arena arena;
    uint64_t previous[3] = {UINT64_C(0), UINT64_C(0), UINT64_C(0)};
    MODEL_CHECK(cerv_conn_arena_init(&arena, slots, 3U));
    for (unsigned round = 0U; round < 32U; ++round) {
        struct cerv_conn *held[3] = {NULL, NULL, NULL};
        for (size_t i = 0U; i < 3U; ++i) {
            MODEL_CHECK(cerv_conn_arena_acquire(&arena, &held[i]) == CERV_SLOT_ACQUIRE_OK);
            MODEL_CHECK(held[i] != NULL);
            if (held[i] != NULL) {
                uint64_t token = cerv_conn_token(held[i]);
                MODEL_CHECK(token != previous[held[i]->index]);
                previous[held[i]->index] = token;
                held[i]->state = CERV_CONN_RECV_HEADERS;
                MODEL_CHECK(cerv_conn_arena_lookup_token(&arena, token) == held[i]);
                held[i]->state = CERV_CONN_FREE;
            }
        }
        {
            struct cerv_conn *extra = NULL;
            MODEL_CHECK(cerv_conn_arena_acquire(&arena, &extra) == CERV_SLOT_ACQUIRE_FULL);
        }
        for (size_t i = 0U; i < 3U; ++i) MODEL_CHECK(cerv_conn_arena_release(&arena, held[i]));
        MODEL_CHECK(arena.active == 0U);
    }
}


static bool model_arena_valid(const struct cerv_conn_arena *arena)
{
    bool seen[3] = {false, false, false};
    size_t free_count = 0U;
    uint32_t index;
    if (arena == NULL || arena->capacity != 3U || arena->slots == NULL || arena->active > arena->capacity) return false;
    index = arena->free_head;
    while (index != CERV_SLOT_NONE) {
        const struct cerv_conn *conn;
        if ((size_t)index >= arena->capacity || seen[index]) return false;
        seen[index] = true;
        conn = &arena->slots[index];
        if (conn->index != index || conn->state != CERV_CONN_FREE || conn->socket_fd != -1 || conn->file_fd != -1) return false;
        ++free_count;
        index = conn->next_free;
    }
    return free_count + arena->active == arena->capacity;
}

static void check_slot_sequences(void)
{
    const unsigned operation_count = 7U;
    const unsigned depth = 4U;
    unsigned sequence_count = 1U;
    for (unsigned i = 0U; i < depth; ++i) sequence_count *= operation_count;
    for (unsigned encoded = 0U; encoded < sequence_count; ++encoded) {
        struct cerv_conn slots[3];
        struct cerv_conn_arena arena;
        bool held[3] = {false, false, false};
        uint64_t tokens[3] = {UINT64_C(0), UINT64_C(0), UINT64_C(0)};
        unsigned code = encoded;
        MODEL_CHECK(cerv_conn_arena_init(&arena, slots, 3U));
        MODEL_CHECK(model_arena_valid(&arena));
        for (unsigned step = 0U; step < depth; ++step) {
            unsigned op = code % operation_count;
            code /= operation_count;
            if (op == 0U) {
                struct cerv_conn *conn = NULL;
                enum cerv_slot_acquire_result result = cerv_conn_arena_acquire(&arena, &conn);
                if (result == CERV_SLOT_ACQUIRE_OK) {
                    MODEL_CHECK(conn != NULL && conn->index < 3U && !held[conn->index]);
                    if (conn != NULL && conn->index < 3U) {
                        held[conn->index] = true;
                        conn->state = CERV_CONN_RECV_HEADERS;
                        tokens[conn->index] = cerv_conn_token(conn);
                        MODEL_CHECK(cerv_conn_arena_lookup_token(&arena, tokens[conn->index]) == conn);
                    }
                } else {
                    MODEL_CHECK(result == CERV_SLOT_ACQUIRE_FULL);
                    MODEL_CHECK(arena.active == arena.capacity);
                }
            } else if (op <= 3U) {
                size_t slot = (size_t)(op - 1U);
                if (held[slot]) {
                    uint64_t stale = tokens[slot];
                    slots[slot].state = CERV_CONN_FREE;
                    MODEL_CHECK(cerv_conn_arena_release(&arena, &slots[slot]));
                    held[slot] = false;
                    MODEL_CHECK(cerv_conn_arena_lookup_token(&arena, stale) == NULL);
                }
            } else {
                size_t slot = (size_t)(op - 4U);
                if (held[slot]) MODEL_CHECK(cerv_conn_arena_lookup_token(&arena, tokens[slot]) == &slots[slot]);
            }
            MODEL_CHECK(model_arena_valid(&arena));
        }
    }
}


static void check_conn_state_classification(void)
{
    for (unsigned state = (unsigned)CERV_CONN_FREE; state <= (unsigned)CERV_CONN_SEND_FALLBACK; ++state) {
        for (uint64_t lifetime = UINT64_C(0); lifetime <= UINT64_C(3); ++lifetime) {
            for (uint64_t header = UINT64_C(0); header <= UINT64_C(3); ++header) {
                for (uint64_t write = UINT64_C(0); write <= UINT64_C(3); ++write) {
                    struct cerv_conn conn = {0};
                    struct cerv_mono_time out = {0};
                    bool is_read;
                    bool is_write;
                    bool have_deadline;
                    conn.state = (enum cerv_conn_state)state;
                    conn.lifetime_deadline.ns = lifetime;
                    conn.header_deadline.ns = header;
                    conn.write_deadline.ns = write;
                    is_read = cerv_conn_wants_read(&conn);
                    is_write = cerv_conn_wants_write(&conn);
                    MODEL_CHECK(is_read == (conn.state == CERV_CONN_RECV_HEADERS));
                    MODEL_CHECK(is_write == (conn.state == CERV_CONN_SEND_HEADERS || conn.state == CERV_CONN_SEND_BODY ||
                                              conn.state == CERV_CONN_SEND_FILE || conn.state == CERV_CONN_SEND_FALLBACK));
                    MODEL_CHECK(!(is_read && is_write));
                    have_deadline = cerv_conn_next_deadline(&conn, &out);
                    MODEL_CHECK(have_deadline == (conn.state != CERV_CONN_FREE));
                    if (have_deadline) {
                        MODEL_CHECK(out.ns <= lifetime);
                        if (is_read) MODEL_CHECK(out.ns == (header < lifetime ? header : lifetime));
                        else if (is_write) MODEL_CHECK(out.ns == (write < lifetime ? write : lifetime));
                        else MODEL_CHECK(out.ns == lifetime);
                    }
                }
            }
        }
    }
}

static void check_keepalive_policy(void)
{
    for (uint32_t completed = UINT32_C(0); completed <= CERV_KEEPALIVE_REQUESTS_MAX + UINT32_C(2); ++completed) {
        for (unsigned request_close = 0U; request_close <= 1U; ++request_close) {
            for (unsigned resource_ok = 0U; resource_ok <= 1U; ++resource_ok) {
                bool close = cerv_conn_should_close_response(request_close != 0U, resource_ok != 0U, completed);
                bool expected = request_close != 0U || resource_ok == 0U ||
                                completed >= CERV_KEEPALIVE_REQUESTS_MAX - UINT32_C(1);
                MODEL_CHECK(close == expected);
                if (!close) MODEL_CHECK(completed < CERV_KEEPALIVE_REQUESTS_MAX - UINT32_C(1));
            }
        }
    }
}

static void check_supervisor_partition(void)
{
    for (size_t total = 1U; total <= 32U; ++total) {
        for (size_t workers = 1U; workers <= total && workers <= 8U; ++workers) {
            size_t sum = 0U;
            size_t smallest = SIZE_MAX;
            size_t largest = 0U;
            for (size_t index = 0U; index < workers; ++index) {
                size_t slots = 0U;
                size_t fds = 0U;
                size_t memory = 0U;
                MODEL_CHECK(cerv_config_partition_slots(total, workers, index, &slots));
                MODEL_CHECK(slots >= 1U);
                MODEL_CHECK(slots == total / workers + (index < total % workers ? 1U : 0U));
                MODEL_CHECK(cerv_runtime_required_worker_fds(slots, &fds));
                MODEL_CHECK(fds == slots * 2U + CERV_FD_SAFETY_MARGIN);
                MODEL_CHECK(cerv_runtime_worker_memory_bytes(slots, &memory));
                MODEL_CHECK(memory == slots * (sizeof(struct cerv_conn) + sizeof(struct cerv_timer_node)));
                sum += slots;
                if (slots < smallest) smallest = slots;
                if (slots > largest) largest = slots;
            }
            MODEL_CHECK(sum == total);
            MODEL_CHECK(largest - smallest <= 1U);
        }
    }
    for (size_t workers = 1U; workers <= 8U; ++workers) {
        for (size_t target = workers; target <= 32U; ++target) {
            for (uint64_t nofile = UINT64_C(0); nofile <= UINT64_C(96); ++nofile) {
                size_t resolved = 0U;
                bool ok = cerv_runtime_auto_connections(workers, target, nofile, false, &resolved);
                if (nofile <= (uint64_t)CERV_FD_SAFETY_MARGIN) {
                    MODEL_CHECK(!ok);
                    continue;
                }
                {
                    uint64_t slots_per_worker =
                        (nofile - (uint64_t)CERV_FD_SAFETY_MARGIN) / UINT64_C(2);
                    uint64_t safe_total = slots_per_worker * (uint64_t)workers;
                    if (slots_per_worker == UINT64_C(0) || safe_total < (uint64_t)workers) {
                        MODEL_CHECK(!ok);
                    } else {
                        size_t expected = safe_total >= (uint64_t)target ? target : (size_t)safe_total;
                        size_t largest = 0U;
                        size_t required_fds = 0U;
                        MODEL_CHECK(ok);
                        MODEL_CHECK(resolved == expected);
                        MODEL_CHECK(resolved >= workers && resolved <= target);
                        MODEL_CHECK(cerv_config_partition_slots(resolved, workers, 0U, &largest));
                        MODEL_CHECK(cerv_runtime_required_worker_fds(largest, &required_fds));
                        MODEL_CHECK((uint64_t)required_fds <= nofile);
                    }
                }
            }
            {
                size_t resolved = 0U;
                MODEL_CHECK(cerv_runtime_auto_connections(workers, target, UINT64_C(0), true, &resolved));
                MODEL_CHECK(resolved == target);
            }
        }
    }
    for (size_t affinity = 1U; affinity <= 16U; ++affinity) {
        for (uint64_t quota = UINT64_C(1); quota <= UINT64_C(8); ++quota) {
            for (uint64_t period = UINT64_C(1); period <= UINT64_C(8); ++period) {
                size_t out = 0U;
                uint64_t ceiling = (quota + period - UINT64_C(1)) / period;
                MODEL_CHECK(cerv_config_auto_worker_count(affinity, true, quota, period, &out));
                MODEL_CHECK(out >= 1U);
                MODEL_CHECK(out <= affinity);
                MODEL_CHECK(out <= (size_t)ceiling || ceiling >= (uint64_t)affinity);
            }
        }
    }
}

int main(void)
{
    check_checked_arithmetic();
    check_decimal();
    check_qvalues();
    check_ranges();
    check_buffers();
    check_paths();
    check_timer_heap();
    check_slot_generation();
    check_slot_sequences();
    check_conn_state_classification();
    check_keepalive_policy();
    check_supervisor_partition();
    if (failures != UINT64_C(0)) {
        fprintf(stderr, "%" PRIu64 " bounded-model failures across %" PRIu64 " checks\n", failures, checks);
        return 1;
    }
    printf("ok: %" PRIu64 " bounded exhaustive checks\n", checks);
    return 0;
}
