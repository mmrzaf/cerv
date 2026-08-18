#include "base/buffer.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>

extern unsigned char nondet_uchar(void);
extern size_t nondet_size_t(void);
void __CPROVER_assume(bool condition);

int main(void)
{
    unsigned char storage[8];
    unsigned char source[8];
    size_t capacity = nondet_size_t();
    size_t len = nondet_size_t();
    struct cerv_buffer buffer;

    __CPROVER_assume(capacity <= sizeof(storage));
    __CPROVER_assume(len <= sizeof(source));
    for (size_t i = 0U; i < sizeof(source); ++i) {
        source[i] = nondet_uchar();
        storage[i] = 0U;
    }
    cerv_buffer_init(&buffer, storage, capacity);
    if (cerv_buffer_append(&buffer, source, len)) {
        assert(buffer.used == len);
        assert(buffer.used <= buffer.capacity);
    } else {
        assert(buffer.used == 0U);
    }

    {
        size_t used = nondet_size_t();
        size_t source_pos = nondet_size_t();
        size_t overlap_len = nondet_size_t();
        unsigned char before[sizeof(storage)];
        unsigned char source_snapshot[sizeof(storage)];
        __CPROVER_assume(used <= sizeof(storage));
        __CPROVER_assume(source_pos <= sizeof(storage));
        __CPROVER_assume(overlap_len <= sizeof(storage) - used);
        __CPROVER_assume(overlap_len <= sizeof(storage) - source_pos);
        for (size_t i = 0U; i < sizeof(storage); ++i) {
            storage[i] = nondet_uchar();
            before[i] = storage[i];
        }
        for (size_t i = 0U; i < overlap_len; ++i) {
            source_snapshot[i] = storage[source_pos + i];
        }
        cerv_buffer_init(&buffer, storage, sizeof(storage));
        buffer.used = used;
        assert(cerv_buffer_append(&buffer, storage + source_pos, overlap_len));
        assert(buffer.used == used + overlap_len);
        for (size_t i = 0U; i < sizeof(storage); ++i) {
            if (i >= used && i < used + overlap_len) {
                assert(storage[i] == source_snapshot[i - used]);
            } else {
                assert(storage[i] == before[i]);
            }
        }
    }
    return 0;
}
