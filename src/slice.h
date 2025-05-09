#pragma once
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    const void* data;
    size_t size;
} Slice;

// Creation
Slice Slice_Make(const void* data, size_t size);

Slice Slice_From(const void* str);

// Comparison
bool Slice_Equals(Slice a, Slice b);

// Utility
Slice Slice_Subslice(Slice slice, size_t start, size_t end);
