#pragma once
#include <stddef.h>
#include <stdbool.h>

typedef struct {
    const void *data;
    size_t size;
} Slice;

// Create a slice from a pointer and length
#define Slice_Make(data_ptr, length) ((Slice){ .data = (data_ptr), .length = (length) })

// Convenience macro for string literals (automatically computes length)
#define Slice_FromString(str) Slice_Make((str), sizeof(str) - 1)

// Check if two slices are equal (byte-wise comparison)
bool Slice_Equals(Slice a, Slice b);
