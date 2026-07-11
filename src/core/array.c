#include "../neo.h"
#include <assert.h>
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static inline void* AlignedAllocPosix(size_t alignment, size_t size)
{
    // POSIX requires alignment be a power-of-two multiple of sizeof(void*)
    assert((alignment & (alignment - 1)) == 0);
    assert(alignment % sizeof(void*) == 0);
    return aligned_alloc(alignment, size);
}

void Array_Init(Array* array, size_t itemSize, size_t capacity, size_t alignment)
{
    assert(itemSize > 0);
    assert(capacity > 0);

    // Default to natural alignment if none specified
    alignment = (alignment > 0) ? alignment : alignof(max_align_t);

    // Ensure the total size is a multiple of alignment
    size_t totalSize = itemSize * capacity;
    size_t alignedSize = totalSize;
    if (alignedSize % alignment != 0) {
        alignedSize = ((totalSize / alignment) + 1) * alignment;
    }

    array->data = AlignedAllocPosix(alignment, alignedSize);
    array->size = 0;
    array->capacity = (array->data != NULL) ? capacity : 0;
    array->itemSize = itemSize;
    array->alignment = alignment;

    LOG_DEBUG("Array initialized: itemSize=%zu, capacity=%zu, alignment=%zu", itemSize, capacity, alignment);
}

void Array_Free(Array* array)
{
    LOG_DEBUG("Array_Free: freeing array %p", (void*)array);
    free(array->data); // aligned_alloc uses standard free()
    array->data = NULL;
    array->size = array->capacity = 0;
}

void Array_Clear(Array* array)
{
    LOG_DEBUG("Array_Clear: clearing array %p (size was %zu)", (void*)array, array->size);
    array->size = 0;
}

bool Array_Append(Array* array, const void* items, size_t count)
{
    if (count == 0)
        return true;
    if (!array->data)
        return false;
    if (!items)
        return false;

    // Resize if needed (geometric growth)
    if (array->size + count > array->capacity) {
        size_t newCapacity = array->capacity * 2 + count;
        LOG_DEBUG("Array resizing capacity from %zu to %zu (need total size %zu)", array->capacity, newCapacity,
            array->size + count);
        size_t totalSize = array->itemSize * newCapacity;
        size_t alignedSize = totalSize;
        if (alignedSize % array->alignment != 0) {
            alignedSize = ((totalSize / array->alignment) + 1) * array->alignment;
        }

        void* newData = AlignedAllocPosix(array->alignment, alignedSize);
        if (!newData)
            return false;

        // Copy existing items
        memcpy(newData, array->data, array->size * array->itemSize);
        free(array->data);
        array->data = newData;
        array->capacity = alignedSize / array->itemSize;
    }

    // Append new items
    memcpy((char*)array->data + (array->size * array->itemSize), items, count * array->itemSize);
    array->size += count;
    LOG_DEBUG("Array_Append: appended %zu items, new size=%zu", count, array->size);
    return true;
}

bool Array_Pop(Array* array)
{
    if (array->size == 0)
        return false;
    array->size--;
    LOG_DEBUG("Array_Pop: popped 1 item, new size=%zu", array->size);
    return true;
}

void* Array_At(const Array* array, size_t index)
{
    assert(index < array->size);
    return (char*)array->data + (index * array->itemSize);
}

void* Array_RawAt(const Array* array, size_t index)
{
    assert(index < array->capacity);
    return (char*)array->data + (index * array->itemSize);
}

size_t Array_Size(const Array* array) { return array->size; }

Slice Array_ToSlice(const Array* array)
{
    return (Slice) { .data = array->data, .size = array->size * array->itemSize };
}
