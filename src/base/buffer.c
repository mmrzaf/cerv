#include "base/buffer.h"

#include "base/checked.h"

#include <string.h>

void cerv_buffer_init(struct cerv_buffer *buf, unsigned char *storage, size_t capacity)
{
    if (buf != NULL) {
        buf->data = storage;
        buf->capacity = capacity;
        buf->used = 0U;
    }
}

bool cerv_buffer_append(struct cerv_buffer *buf, const void *src, size_t len)
{
    size_t next = 0U;

    if (buf == NULL || (len != 0U && src == NULL) || (buf->capacity != 0U && buf->data == NULL)) {
        return false;
    }
    if (!cerv_size_add(buf->used, len, &next) || next > buf->capacity) {
        return false;
    }
    if (len != 0U) {
        memmove(buf->data + buf->used, src, len);
    }
    buf->used = next;
    return true;
}

bool cerv_buffer_append_byte(struct cerv_buffer *buf, unsigned char byte)
{
    return cerv_buffer_append(buf, &byte, 1U);
}

bool cerv_buffer_append_u64(struct cerv_buffer *buf, uint64_t value)
{
    unsigned char digits[20];
    size_t count = 0U;
    size_t i = 0U;

    do {
        digits[count] = (unsigned char)('0' + (unsigned char)(value % UINT64_C(10)));
        ++count;
        value /= UINT64_C(10);
    } while (value != UINT64_C(0));

    for (i = 0U; i < count / 2U; ++i) {
        unsigned char tmp = digits[i];
        digits[i] = digits[count - 1U - i];
        digits[count - 1U - i] = tmp;
    }
    return cerv_buffer_append(buf, digits, count);
}
