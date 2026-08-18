#include "runtime/timer.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static bool cerv_timer_less(const struct cerv_timer_node *a, const struct cerv_timer_node *b)
{
    if (a->deadline_ns != b->deadline_ns) return a->deadline_ns < b->deadline_ns;
    if (a->slot_index != b->slot_index) return a->slot_index < b->slot_index;
    return a->generation < b->generation;
}

static void cerv_timer_swap(struct cerv_timer_heap *heap, size_t a, size_t b)
{
    struct cerv_timer_node tmp = heap->nodes[a];
    heap->nodes[a] = heap->nodes[b];
    heap->nodes[b] = tmp;
    heap->nodes[a].link->heap_index = a;
    heap->nodes[b].link->heap_index = b;
}

static void cerv_timer_sift_up(struct cerv_timer_heap *heap, size_t pos)
{
    while (pos > 0U) {
        size_t parent = (pos - 1U) / 2U;
        if (!cerv_timer_less(&heap->nodes[pos], &heap->nodes[parent])) break;
        cerv_timer_swap(heap, pos, parent);
        pos = parent;
    }
}

static void cerv_timer_sift_down(struct cerv_timer_heap *heap, size_t pos)
{
    for (;;) {
        size_t left;
        size_t right;
        size_t best;
        if (heap->size < 2U || pos > (heap->size - 2U) / 2U) break;
        left = pos * 2U + 1U;
        right = left + 1U;
        best = left;
        if (right < heap->size && cerv_timer_less(&heap->nodes[right], &heap->nodes[left])) best = right;
        if (!cerv_timer_less(&heap->nodes[best], &heap->nodes[pos])) break;
        cerv_timer_swap(heap, pos, best);
        pos = best;
    }
}

void cerv_timer_link_init(struct cerv_timer_link *link)
{
    if (link != NULL) link->heap_index = CERV_TIMER_NOT_IN_HEAP;
}

bool cerv_timer_heap_init(struct cerv_timer_heap *heap, struct cerv_timer_node *storage, size_t capacity)
{
    if (heap == NULL || (storage == NULL && capacity != 0U)) return false;
    heap->nodes = storage;
    heap->size = 0U;
    heap->capacity = capacity;
    return true;
}

bool cerv_timer_set(struct cerv_timer_heap *heap, struct cerv_timer_link *link, uint32_t slot_index,
                    uint32_t generation, uint64_t deadline_ns)
{
    size_t pos;
    uint64_t old_deadline;
    if (heap == NULL || link == NULL) return false;
    if (link->heap_index == CERV_TIMER_NOT_IN_HEAP) {
        if (heap->size >= heap->capacity) return false;
        pos = heap->size++;
        heap->nodes[pos] = (struct cerv_timer_node){
            .link = link,
            .deadline_ns = deadline_ns,
            .slot_index = slot_index,
            .generation = generation
        };
        link->heap_index = pos;
        cerv_timer_sift_up(heap, pos);
        return true;
    }
    pos = link->heap_index;
    if (pos >= heap->size || heap->nodes[pos].link != link) return false;
    old_deadline = heap->nodes[pos].deadline_ns;
    heap->nodes[pos].deadline_ns = deadline_ns;
    heap->nodes[pos].slot_index = slot_index;
    heap->nodes[pos].generation = generation;
    if (deadline_ns < old_deadline) cerv_timer_sift_up(heap, pos);
    else if (deadline_ns > old_deadline) cerv_timer_sift_down(heap, pos);
    else {
        cerv_timer_sift_up(heap, pos);
        pos = link->heap_index;
        cerv_timer_sift_down(heap, pos);
    }
    return true;
}

bool cerv_timer_remove(struct cerv_timer_heap *heap, struct cerv_timer_link *link)
{
    size_t pos;
    size_t last;
    if (heap == NULL || link == NULL) return false;
    if (link->heap_index == CERV_TIMER_NOT_IN_HEAP) return true;
    pos = link->heap_index;
    if (pos >= heap->size || heap->nodes[pos].link != link) return false;
    last = heap->size - 1U;
    link->heap_index = CERV_TIMER_NOT_IN_HEAP;
    if (pos != last) {
        heap->nodes[pos] = heap->nodes[last];
        heap->nodes[pos].link->heap_index = pos;
    }
    --heap->size;
    if (pos < heap->size) {
        if (pos > 0U && cerv_timer_less(&heap->nodes[pos], &heap->nodes[(pos - 1U) / 2U])) {
            cerv_timer_sift_up(heap, pos);
        } else {
            cerv_timer_sift_down(heap, pos);
        }
    }
    return true;
}

bool cerv_timer_peek(const struct cerv_timer_heap *heap, struct cerv_timer_node *out)
{
    if (heap == NULL || out == NULL || heap->size == 0U) return false;
    *out = heap->nodes[0];
    return true;
}

bool cerv_timer_pop(struct cerv_timer_heap *heap, struct cerv_timer_node *out)
{
    struct cerv_timer_node first;
    if (!cerv_timer_peek(heap, &first)) return false;
    if (!cerv_timer_remove(heap, first.link)) return false;
    *out = first;
    return true;
}

bool cerv_timer_heap_valid(const struct cerv_timer_heap *heap)
{
    size_t i;
    if (heap == NULL || (heap->nodes == NULL && heap->capacity != 0U) || heap->size > heap->capacity) return false;
    for (i = 0U; i < heap->size; ++i) {
        if (heap->nodes[i].link == NULL || heap->nodes[i].link->heap_index != i) return false;
        if (heap->size >= 2U && i <= (heap->size - 2U) / 2U) {
            size_t left = i * 2U + 1U;
            size_t right = left + 1U;
            if (cerv_timer_less(&heap->nodes[left], &heap->nodes[i])) return false;
            if (right < heap->size && cerv_timer_less(&heap->nodes[right], &heap->nodes[i])) return false;
        }
    }
    return true;
}
