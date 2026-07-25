#include "array.h"
#include "logger.h"
#include <assert.h>
#include <stdalign.h>
#include <stdint.h>
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

size_t ArrayComputeAllocationSize(size_t itemSize, size_t capacity, size_t alignment)
{
    if (itemSize != 0 && capacity > SIZE_MAX / itemSize)
        return 0; // multiplication would overflow
    size_t totalSize = itemSize * capacity;
    if (alignment == 0 || totalSize % alignment == 0)
        return totalSize;
    size_t alignedSize = ((totalSize / alignment) + 1) * alignment;
    if (alignedSize < totalSize)
        return 0; // rounding up overflowed
    return alignedSize;
}

void Array_Init(Array* array, size_t itemSize, size_t capacity, size_t alignment)
{
    assert(itemSize > 0);
    assert(capacity > 0);

    // Default to natural alignment if none specified
    alignment = (alignment > 0) ? alignment : alignof(max_align_t);
    if (alignment < sizeof(void*)) {
        alignment = sizeof(void*);
    }

    // Ensure the total size is a multiple of alignment
    size_t alignedSize = ArrayComputeAllocationSize(itemSize, capacity, alignment);

    array->data = (alignedSize > 0) ? AlignedAllocPosix(alignment, alignedSize) : NULL;
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
        size_t alignedSize = ArrayComputeAllocationSize(array->itemSize, newCapacity, array->alignment);
        if (alignedSize == 0)
            return false;

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

bool Array_ReplaceRange(Array* array, size_t start, size_t count, const void* newItems, size_t newCount)
{
    assert(start <= array->size);
    assert(start + count <= array->size);
    if (!array->data)
        return false;

    size_t tailCount = array->size - (start + count);
    size_t newSize = start + newCount + tailCount;

    if (newSize > array->capacity) {
        size_t newCapacity = array->capacity * 2 + (newSize - array->capacity);
        size_t alignedSize = ArrayComputeAllocationSize(array->itemSize, newCapacity, array->alignment);
        if (alignedSize == 0)
            return false;

        void* newData = AlignedAllocPosix(array->alignment, alignedSize);
        if (!newData)
            return false;

        memcpy(newData, array->data, start * array->itemSize);
        if (newCount > 0 && newItems) {
            memcpy((char*)newData + start * array->itemSize, newItems, newCount * array->itemSize);
        }
        memcpy((char*)newData + (start + newCount) * array->itemSize,
            (char*)array->data + (start + count) * array->itemSize, tailCount * array->itemSize);

        free(array->data);
        array->data = newData;
        array->capacity = alignedSize / array->itemSize;
        array->size = newSize;
        LOG_DEBUG("Array_ReplaceRange: grew capacity to %zu, new size=%zu", array->capacity, array->size);
        return true;
    }

    if (tailCount > 0 && count != newCount) {
        memmove((char*)array->data + (start + newCount) * array->itemSize,
            (char*)array->data + (start + count) * array->itemSize, tailCount * array->itemSize);
    }
    if (newCount > 0 && newItems) {
        memcpy((char*)array->data + start * array->itemSize, newItems, newCount * array->itemSize);
    }
    array->size = newSize;
    LOG_DEBUG("Array_ReplaceRange: replaced %zu items at %zu with %zu items, new size=%zu", count, start, newCount,
        array->size);
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
