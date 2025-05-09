#include "buffer.h"
#include <assert.h>
#include <stdalign.h>
#include <stdlib.h>
#include <string.h>

static inline void* aligned_alloc_posix(size_t alignment, size_t size)
{
    // POSIX requires alignment be a power-of-two multiple of sizeof(void*)
    assert((alignment & (alignment - 1)) == 0);
    assert(alignment % sizeof(void*) == 0);
    return aligned_alloc(alignment, size);
}

void Buffer_Init(Buffer* buffer, size_t itemSize, size_t capacity, size_t alignment)
{
    assert(itemSize > 0);
    assert(capacity > 0);

    // Default to natural alignment if none specified
    alignment = (alignment > 0) ? alignment : alignof(max_align_t);

    buffer->data = aligned_alloc_posix(alignment, itemSize * capacity);
    buffer->size = 0;
    buffer->capacity = (buffer->data != NULL) ? capacity : 0;
    buffer->itemSize = itemSize;
    buffer->alignment = alignment;
}

void Buffer_Free(Buffer* buffer)
{
    free(buffer->data); // aligned_alloc uses standard free()
    buffer->data = NULL;
    buffer->size = buffer->capacity = 0;
}

void Buffer_Clear(Buffer* buffer)
{
    buffer->size = 0;
}

bool Buffer_Append(Buffer* buffer, const void* items, size_t count)
{
    if (count == 0)
        return true;
    if (!buffer->data)
        return false;

    // Resize if needed (geometric growth)
    if (buffer->size + count > buffer->capacity) {
        size_t new_cap = buffer->capacity * 2 + count;
        void* new_data = aligned_alloc_posix(buffer->alignment, buffer->itemSize * new_cap);
        if (!new_data)
            return false;

        // Copy existing items
        memcpy(new_data, buffer->data, buffer->size * buffer->itemSize);
        free(buffer->data);
        buffer->data = new_data;
        buffer->capacity = new_cap;
    }

    // Append new items
    memcpy(
        (char*)buffer->data + (buffer->size * buffer->itemSize),
        items,
        count * buffer->itemSize);
    buffer->size += count;
    return true;
}

bool Buffer_Pop(Buffer* buffer)
{
    if (buffer->size == 0)
        return false;
    buffer->size--;
    return true;
}

void* Buffer_At(Buffer* buffer, size_t index)
{
    assert(index < buffer->size);
    return (char*)buffer->data + (index * buffer->itemSize);
}

size_t Buffer_Size(Buffer* buffer)
{
    return buffer->size;
}

Slice Buffer_ToSlice(Buffer* buffer)
{
    return (Slice) {
        .data = buffer->data,
        .size = buffer->size * buffer->itemSize
    };
}
