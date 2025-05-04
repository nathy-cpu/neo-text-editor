#pragma once
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    void* data;
    size_t size;
    size_t capacity;
} Buffer;

// Initialize a buffer with a default capacity
void Buffer_Init(Buffer* buffer, size_t capacity);

// Free the buffer's memory and reset fields
void Buffer_Free(Buffer* buffer);

// Append data to the buffer (grows if needed)
bool Buffer_Append(Buffer* buffer, const void* items, size_t count);

// Clear the buffer (retains capacity)
void Buffer_Clear(Buffer* buffer);
