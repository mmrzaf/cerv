#ifndef CERV_BUFFER_H
#define CERV_BUFFER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct cerv_buffer {
    unsigned char *data;
    size_t capacity;
    size_t used;
};

void cerv_buffer_init(struct cerv_buffer *buf, unsigned char *storage, size_t capacity);
bool cerv_buffer_append(struct cerv_buffer *buf, const void *src, size_t len);
bool cerv_buffer_append_byte(struct cerv_buffer *buf, unsigned char byte);
bool cerv_buffer_append_u64(struct cerv_buffer *buf, uint64_t value);

#endif
