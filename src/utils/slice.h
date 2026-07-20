#pragma once

#include <stdbool.h>
#include <stddef.h>

// Slice - Type-agnostic slice/string view
typedef struct {
    const void* data;
    size_t size;
} Slice;

/**
 * @brief Creates a new Slice from a raw pointer and size.
 * @param data Pointer to the start of the data.
 * @param size Number of bytes in the slice.
 * @return A constructed Slice view.
 */
Slice Slice_Make(const void* data, size_t size);

/**
 * @brief Creates a new Slice from a null-terminated C string.
 * @param string Null-terminated string to view.
 * @return A constructed Slice view.
 */
Slice Slice_From(const void* string);

/**
 * @brief Compares two Slices for exact byte-for-byte equality.
 * @param a First slice.
 * @param b Second slice.
 * @return True if they are identical in size and content, false otherwise.
 */
bool Slice_Equals(Slice a, Slice b);

/**
 * @brief Creates a sub-slice from an existing slice.
 * @param slice The original slice.
 * @param start The starting byte index.
 * @param end The ending byte index (exclusive).
 * @return A new Slice representing the specified range.
 */
Slice Slice_Subslice(Slice slice, size_t start, size_t end);
