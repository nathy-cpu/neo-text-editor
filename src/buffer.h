#pragma once
#include <stddef.h>
#include <stdbool.h>

typedef struct {
    void *data;
    size_t len;
    size_t cap;
} Buffer;

// Initialize a buffer with a default capacity
Buffer* Buffer_New(size_t capacity);

// Free the buffer's memory and reset fields
void Buffer_Free(Buffer *buffer);

// Append data to the buffer (grows if needed)
bool Buffer_Append(Buffer *buffer, const void *items, size_t numOfItems);

// Clear the buffer (retains capacity)
void Buffer_Clear(Buffer *buffer);
