#pragma once
#include "buffer.h"
#include "slice.h"
#include <stddef.h>

typedef struct {
    Buffer data;
    size_t gapStart;
    size_t gapEnd;

} GapBuffer;

// Initialize with initial capacity
void GapBuffer_Init(GapBuffer* gapBuffer, size_t initialCapacity, size_t alignment);

// Free resources
void GapBuffer_Free(GapBuffer* gapBuffer);

// Core operations
void GapBuffer_InsertSlice(GapBuffer* gapBuffer, size_t position, Slice content);

void GapBuffer_InsertChar(GapBuffer* gapBuffer, size_t position, char content);

void GapBuffer_Delete(GapBuffer* gapBuffer, size_t position, size_t size);

Slice GapBuffer_ToSlice(GapBuffer* gapBuffer); // Get all content as slice

void GapBuffer_Clear(GapBuffer* gapBuffer);

// Utility
size_t GapBuffer_Size(GapBuffer* gapBuffer);

void GapBuffer_MoveGap(GapBuffer* gapBuffer, size_t newGapStart);
