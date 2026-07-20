#pragma once

#include "array.h"
#include "slice.h"
#include <stddef.h>
#include <stdint.h>

// GapBuffer - Type-agnostic gap buffer
typedef struct {
    Array data;
    uint32_t gapStart;
    uint32_t gapEnd;
} GapBuffer;

/**
 * @brief Initializes a gap buffer with a specific item size.
 */
void GapBuffer_Init(GapBuffer* gapBuffer, size_t itemSize, size_t initialCapacity, size_t alignment);

/**
 * @brief Frees resources associated with the gap buffer.
 */
void GapBuffer_Free(GapBuffer* gapBuffer);

/**
 * @brief Gets a pointer to the item at a specific logical position in the gap buffer.
 */
void* GapBuffer_At(const GapBuffer* gapBuffer, size_t index);

/**
 * @brief Inserts a slice of data at a logical position in the gap buffer.
 */
void GapBuffer_InsertSlice(GapBuffer* gapBuffer, size_t position, Slice content);

/**
 * @brief Inserts a single character at a logical position (for char gap buffers).
 */
void GapBuffer_InsertChar(GapBuffer* gapBuffer, size_t position, char content);

/**
 * @brief Deletes a sequence of items from the gap buffer.
 */
void GapBuffer_Delete(GapBuffer* gapBuffer, size_t position, size_t size);

/**
 * @brief Flattens a char gap buffer into a dynamically allocated contiguous slice.
 */
Slice GapBuffer_ToSlice(GapBuffer* gapBuffer);

/**
 * @brief Clears the entire gap buffer.
 */
void GapBuffer_Clear(GapBuffer* gapBuffer);

/**
 * @brief Returns the total logical size (amount of elements) in the gap buffer.
 */
size_t GapBuffer_Size(const GapBuffer* gapBuffer);

/**
 * @brief Moves the gap to the specified new logical position.
 */
void GapBuffer_MoveGap(GapBuffer* gapBuffer, size_t newGapStart);
