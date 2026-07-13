#include "../neo.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define MIN_GAP_SIZE 64 // Align to cache lines

static void EnsureGapCapacity(GapBuffer* gb, size_t required)
{
    if ((gb->gapEnd - gb->gapStart) >= required)
        return;

    size_t newCapacity = gb->data.capacity * 2 + required;
    size_t newGapSize = newCapacity - gb->data.size;

    LOG_DEBUG("EnsureGapCapacity: expanding gap buffer capacity from %zu to %zu (required=%zu)", gb->data.capacity,
        newCapacity, required);

    Array newData;
    Array_Init(&newData, gb->data.itemSize, newCapacity, gb->data.alignment);

    // Copy data before gap
    Array_Append(&newData, gb->data.data, gb->gapStart);
    // Copy data after gap
    Array_Append(&newData, (char*)gb->data.data + (gb->gapEnd * gb->data.itemSize), gb->data.size - gb->gapStart);

    Array_Free(&gb->data);
    gb->data = newData;
    gb->gapEnd = gb->gapStart + newGapSize;
}

void GapBuffer_Init(GapBuffer* gb, size_t itemSize, size_t initialCapacity, size_t alignment)
{
    assert(gb);
    Array_Init(&gb->data, itemSize, initialCapacity + MIN_GAP_SIZE, alignment);
    gb->gapStart = 0;
    gb->gapEnd = gb->data.capacity;
    LOG_DEBUG(
        "GapBuffer initialized: itemSize=%zu, capacity=%zu, alignment=%zu", itemSize, gb->data.capacity, alignment);
}

void GapBuffer_Free(GapBuffer* gb)
{
    if (!gb)
        return;
    LOG_DEBUG("GapBuffer_Free: freeing gap buffer %p", (void*)gb);
    Array_Free(&gb->data);
    gb->gapStart = gb->gapEnd = 0;
}

void* GapBuffer_At(const GapBuffer* gb, size_t index)
{
    assert(index < gb->data.size);
    if (index < gb->gapStart) {
        return (char*)gb->data.data + (index * gb->data.itemSize);
    } else {
        return (char*)gb->data.data + ((index + (gb->gapEnd - gb->gapStart)) * gb->data.itemSize);
    }
}

void GapBuffer_MoveGap(GapBuffer* gb, size_t newGapStart)
{
    assert(newGapStart <= gb->data.size);

    if (newGapStart == gb->gapStart)
        return;

    LOG_DEBUG("GapBuffer_MoveGap: shifting gap from %zu to %zu", gb->gapStart, newGapStart);

    size_t gapSize = gb->gapEnd - gb->gapStart;
    void* src = (newGapStart > gb->gapStart) ? Array_RawAt(&gb->data, gb->gapEnd) : Array_RawAt(&gb->data, newGapStart);
    void* dest = (newGapStart > gb->gapStart) ? Array_RawAt(&gb->data, gb->gapStart)
                                              : Array_RawAt(&gb->data, newGapStart + gapSize);

    size_t moveSize = abs((int)newGapStart - (int)gb->gapStart) * gb->data.itemSize;
    memmove(dest, src, moveSize);

    gb->gapStart = newGapStart;
    gb->gapEnd = newGapStart + gapSize;
}

void GapBuffer_InsertSlice(GapBuffer* gb, size_t position, Slice content)
{
    if (content.size == 0)
        return;

    assert(content.size % gb->data.itemSize == 0);
    size_t count = content.size / gb->data.itemSize;

    GapBuffer_MoveGap(gb, position);
    EnsureGapCapacity(gb, count);

    memcpy(Array_RawAt(&gb->data, gb->gapStart), content.data, content.size);
    gb->gapStart += count;
    gb->data.size += count;
    LOG_DEBUG("GapBuffer_InsertSlice: inserted %zu items at position %zu", count, position);
}

void GapBuffer_InsertChar(GapBuffer* gb, size_t position, char character)
{
    assert(gb->data.itemSize == sizeof(char));
    GapBuffer_MoveGap(gb, position);
    EnsureGapCapacity(gb, 1);

    *(char*)Array_RawAt(&gb->data, gb->gapStart) = character;
    gb->gapStart++;
    gb->data.size++;
    LOG_DEBUG("GapBuffer_InsertChar: inserted char '%c' at position %zu", character, position);
}

void GapBuffer_Delete(GapBuffer* gb, size_t position, size_t size)
{
    if (size == 0)
        return;

    GapBuffer_MoveGap(gb, position + size);
    gb->gapStart -= size;
    gb->data.size -= size;
    LOG_DEBUG("GapBuffer_Delete: deleted %zu items starting from %zu", size, position);
}

Slice GapBuffer_ToSlice(GapBuffer* gb)
{
    GapBuffer_MoveGap(gb, gb->data.size); // Collapse gap
    return (Slice) { .data = gb->data.data, .size = gb->data.size * gb->data.itemSize };
}

void GapBuffer_Clear(GapBuffer* gb)
{
    LOG_DEBUG("GapBuffer_Clear: clearing gap buffer %p", (void*)gb);
    gb->gapStart = 0;
    gb->gapEnd = gb->data.capacity;
    gb->data.size = 0;
}

size_t GapBuffer_Size(const GapBuffer* gb) { return gb->data.size; }
