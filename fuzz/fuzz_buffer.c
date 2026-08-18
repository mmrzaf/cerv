#include "base/buffer.h"
#include "fuzz_support.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    unsigned char storage[64];
    unsigned char before[64];
    unsigned char expected[64];
    struct cerv_buffer buffer;
    size_t capacity = size == 0U ? 0U : (size_t)(data[0] % (sizeof(storage) + 1U));
    const uint8_t *payload = size == 0U ? data : data + 1U;
    size_t payload_len = size == 0U ? 0U : size - 1U;
    bool ok;

    memset(storage, 0xa5, sizeof(storage));
    memcpy(before, storage, sizeof(storage));
    cerv_buffer_init(&buffer, storage, capacity);
    ok = cerv_buffer_append(&buffer, payload, payload_len);
    if (ok) {
        cerv_fuzz_require(buffer.used == payload_len);
        cerv_fuzz_require(buffer.used <= capacity);
        if (payload_len != 0U) cerv_fuzz_require(memcmp(storage, payload, payload_len) == 0);
    } else {
        cerv_fuzz_require(buffer.used == 0U);
        cerv_fuzz_require(memcmp(storage, before, sizeof(storage)) == 0);
    }

    /* Exercise the public overlap-safe append contract with an in-buffer source. */
    memset(storage, 0xa5, sizeof(storage));
    if (size != 0U) {
        size_t copied = size < sizeof(storage) ? size : sizeof(storage);
        memcpy(storage, data, copied);
    }
    memcpy(expected, storage, sizeof(storage));
    cerv_buffer_init(&buffer, storage, sizeof(storage));
    {
        size_t used = size > 0U ? (size_t)(data[0] % 33U) : 16U;
        size_t source = size > 1U ? (size_t)(data[1] % 64U) : 8U;
        size_t max_len = sizeof(storage) - used;
        size_t source_room = sizeof(storage) - source;
        size_t requested = size > 2U ? (size_t)data[2] : 24U;
        size_t overlap_len = requested;
        if (overlap_len > max_len) overlap_len = max_len;
        if (overlap_len > source_room) overlap_len = source_room;
        buffer.used = used;
        memmove(expected + used, expected + source, overlap_len);
        cerv_fuzz_require(cerv_buffer_append(&buffer, storage + source, overlap_len));
        cerv_fuzz_require(buffer.used == used + overlap_len);
        cerv_fuzz_require(memcmp(storage, expected, sizeof(storage)) == 0);
    }
    return 0;
}
