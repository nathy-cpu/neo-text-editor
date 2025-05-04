#include "buffer.h"
#include <stdlib.h>
#include <string.h>

void Buffer_Init(Buffer* buffer, size_t capacity)
{
    buffer->data = malloc(capacity);
    buffer->size = 0;
    buffer->capacity = (buffer->data != NULL) ? capacity : 0;
}

void Buffer_Free(Buffer* buffer)
{
    free(buffer->data);
    buffer->data = NULL;
    buffer->size = buffer->capacity = 0;
}

bool Buffer_Append(Buffer* buffer, const void* items, size_t count)
{
    if (!buffer || count == 0)
        return true; // Early exit for empty appends
    if (items == NULL)
        return false; // Explicit NULL check

    if (buffer->size + count > buffer->capacity) {
        size_t new_cap = buffer->capacity * 2 + count;
        void* new_data = realloc(buffer->data, new_cap);
        if (!new_data)
            return false;
        buffer->data = new_data;
        buffer->capacity = new_cap;
    }

    memcpy((char*)buffer->data + buffer->size, items, count);
    buffer->size += count;
    return true;
}

void Buffer_Clear(Buffer* buffer) { buffer->size = 0; }
