#pragma once

#include <stdalign.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Array - Type-agnostic arraylist
typedef struct {
    void* data;
    uint32_t size;
    uint32_t capacity;
    uint32_t itemSize;
    uint32_t alignment;
} Array;

/**
 * @brief Initializes a dynamic array.
 * @param array Pointer to the array to initialize.
 * @param itemSize Size of a single item (e.g., sizeof(int)).
 * @param capacity Initial capacity in items.
 * @param alignment Memory alignment requirement.
 */
void Array_Init(Array* array, size_t itemSize, size_t capacity, size_t alignment);

/**
 * @brief Frees the underlying memory of the array.
 * @param array Pointer to the array to free.
 */
void Array_Free(Array* array);

/**
 * @brief Clears the array by resetting its size to 0 (does not free memory).
 * @param array Pointer to the array to clear.
 */
void Array_Clear(Array* array);

/**
 * @brief Appends multiple items to the end of the array, resizing if necessary.
 * @param array Pointer to the array.
 * @param items Pointer to the items to append.
 * @param count Number of items to append.
 * @return True on success, false on allocation failure.
 */
bool Array_Append(Array* array, const void* items, size_t count);

/**
 * @brief Removes the last item from the array.
 * @param array Pointer to the array.
 * @return True if an item was popped, false if the array was empty.
 */
bool Array_Pop(Array* array);

/**
 * @brief Replaces a range of items in-place with a different number of items,
 * shifting the tail as needed (memmove-based; only safe for POD element types
 * with no owned pointers).
 * @param array Pointer to the array.
 * @param start Index of the first item to replace.
 * @param count Number of existing items to remove starting at `start`.
 * @param newItems Pointer to the replacement items (may be NULL if newCount is 0).
 * @param newCount Number of replacement items.
 * @return True on success, false on allocation failure.
 */
bool Array_ReplaceRange(Array* array, size_t start, size_t count, const void* newItems, size_t newCount);

/**
 * @brief Gets a pointer to the item at the specified index.
 * @param array Pointer to the array.
 * @param index Index of the item.
 * @return Pointer to the item, or NULL if out of bounds (based on logical size).
 */
void* Array_At(const Array* array, size_t index);

/**
 * @brief Gets a raw pointer to an item at an index up to the capacity.
 * @param array Pointer to the array.
 * @param index Index of the item.
 * @return Pointer to the item, or NULL if out of bounds (based on capacity).
 */
void* Array_RawAt(const Array* array, size_t index);

/**
 * @brief Returns the number of items currently in the array.
 * @param array Pointer to the array.
 * @return Number of items.
 */
size_t Array_Size(const Array* array);

// Note: To avoid circular dependency with Slice, Array_ToSlice is declared here but returns Slice.
// We must forward declare Slice if not included. Since we include slice.h, or we can just forward declare it.
// Let's include "slice.h" so we can return a Slice.
#include "slice.h"

/**
 * @brief Returns a Slice representing the entire array.
 * @param array Pointer to the array.
 * @return A Slice view of the array's used data.
 */
Slice Array_ToSlice(const Array* array);

// Type-safe macros
#define Array_InitChar(array, capacity) Array_Init(array, sizeof(char), capacity, 64) // Cache line
#define Array_InitStruct(array, type, capacity) Array_Init(array, sizeof(type), capacity, alignof(type))
#define Array_Get(array, type, index) (*(type*)Array_At(array, index))
#define Array_AppendSlice(array, slice) Array_Append(array, slice.data, slice.size)
