#include "buffer.h"
#include <stdlib.h>
#include <string.h>

Buffer* Buffer_New(size_t capacity) {
    Buffer* buffer;
    buffer->data = malloc(capacity);
    buffer->len = 0;
    buffer->cap = (buffer->data != NULL) ? capacity : 0;
    return buffer;
}

void Buffer_Free(Buffer *buffer) {
    free(buffer->data);
    buffer->data = NULL;
    buffer->len = buffer->cap = 0;
}

bool Buffer_Append(Buffer *buffer, const void *items, size_t count) {
    if (buffer->len + count > buffer->cap) {
        size_t new_cap = buffer->cap * 2 + count; // Geometric growth
        void *new_data = realloc(buffer->data, new_cap);
        if (!new_data) return false;
        buffer->data = new_data;
        buffer->cap = new_cap;
    }
    memcpy((char *)buffer->data + buffer->len, items, count);
    buffer->len += count;
    return true;
}

void Buffer_Clear(Buffer *buffer) {
    buffer->len = 0;
}
