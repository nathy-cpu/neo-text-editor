#pragma once
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    const void* data;
    size_t size;
} Slice;

// Create a slice from a pointer and length
Slice Slice_Make(const void* data, size_t size);

// Convenience macro for string literals (automatically computes length)
Slice Slice_From(const void* str);

// Check if two slices are equal (byte-wise comparison)
bool Slice_Equals(Slice a, Slice b);
