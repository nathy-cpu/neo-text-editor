#pragma once
#include "slice.h"
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    void* data;
    size_t size;
    size_t capacity;
    size_t itemSize;
    size_t alignment;

} Buffer;

// Life cycle
void Buffer_Init(Buffer* buffer, size_t itemSize, size_t capacity, size_t alignment);

void Buffer_Free(Buffer* buffer);

void Buffer_Clear(Buffer* buffer);

// Operation
bool Buffer_Append(Buffer* buffer, const void* items, size_t count);

bool Buffer_Pop(Buffer* buffer);

// Access
void* Buffer_At(Buffer* buffer, size_t index);

size_t Buffer_Size(Buffer* buffer);

Slice Buffer_ToSlice(Buffer* buffer);

// Type-safe macros
#define Buffer_InitChar(buf, cap) Buffer_Init(buf, sizeof(char), cap, 64) // Cache line

#define Buffer_InitStruct(buf, type, cap) Buffer_Init(buf, sizeof(type), cap, alignof(type))

#define Buffer_Get(buf, type, idx) (*(type*)Buffer_At(buf, idx))

#define Buffer_AppendSlice(buf, slice) Buffer_Append(buf, slice.data, slice.size)
