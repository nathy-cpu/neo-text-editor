#include "../src/utils/array.h"
#include "../src/utils/slice.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

TEST(array, array_basic)
{
    Array arr;
    Array_InitChar(&arr, 10);
    
    assert(Array_Size(&arr) == 0 && "New array size should be 0");
    
    Array_Append(&arr, "hello", 5);
    assert(Array_Size(&arr) == 5 && "Size should be 5 after append");
    
    Slice s = Array_ToSlice(&arr);
    assert(s.size == 5);
    assert(memcmp(s.data, "hello", 5) == 0);
    
    Array_Pop(&arr);
    assert(Array_Size(&arr) == 4 && "Size should be 4 after pop");
    
    Array_Clear(&arr);
    assert(Array_Size(&arr) == 0 && "Size should be 0 after clear");
    
    Array_Free(&arr);
}

TEST(array, array_growth)
{
    Array arr;
    Array_InitChar(&arr, 2);

    for (int i = 0; i < 1000; i++) {
        char c = 'A' + (i % 26);
        Array_Append(&arr, &c, 1);
    }

    assert(Array_Size(&arr) == 1000 && "Size should be 1000 after appends");
    assert(arr.capacity >= 1000 && "Capacity should grow to accommodate elements");

    Array_Free(&arr);
}

TEST(array, array_replace_range_shrink)
{
    Array arr;
    Array_InitChar(&arr, 16);
    Array_Append(&arr, "hello world", 11);

    // Replace "world" (5 chars) with "there" (5 chars) -- same size, middle of array.
    Array_ReplaceRange(&arr, 6, 5, "there", 5);
    assert(Array_Size(&arr) == 11);
    Slice s = Array_ToSlice(&arr);
    assert(memcmp(s.data, "hello there", 11) == 0);

    // Replace "hello " (6 chars) with "hi " (3 chars) -- shrinks the array.
    Array_ReplaceRange(&arr, 0, 6, "hi ", 3);
    assert(Array_Size(&arr) == 8);
    s = Array_ToSlice(&arr);
    assert(memcmp(s.data, "hi there", 8) == 0);

    Array_Free(&arr);
}

TEST(array, array_replace_range_grow)
{
    Array arr;
    Array_InitChar(&arr, 4);
    Array_Append(&arr, "ac", 2);

    // Insert "b" between 'a' and 'c' -- grows the array, no capacity available yet.
    Array_ReplaceRange(&arr, 1, 0, "b", 1);
    assert(Array_Size(&arr) == 3);
    Slice s = Array_ToSlice(&arr);
    assert(memcmp(s.data, "abc", 3) == 0);

    // Delete the middle character.
    Array_ReplaceRange(&arr, 1, 1, NULL, 0);
    assert(Array_Size(&arr) == 2);
    s = Array_ToSlice(&arr);
    assert(memcmp(s.data, "ac", 2) == 0);

    Array_Free(&arr);
}

TEST(array, array_replace_range_ends)
{
    Array arr;
    Array_InitChar(&arr, 4);
    Array_Append(&arr, "bcd", 3);

    // Insert at the very start.
    Array_ReplaceRange(&arr, 0, 0, "a", 1);
    assert(Array_Size(&arr) == 4);
    Slice s = Array_ToSlice(&arr);
    assert(memcmp(s.data, "abcd", 4) == 0);

    // Append at the very end.
    Array_ReplaceRange(&arr, Array_Size(&arr), 0, "e", 1);
    assert(Array_Size(&arr) == 5);
    s = Array_ToSlice(&arr);
    assert(memcmp(s.data, "abcde", 5) == 0);

    // Remove the whole array in one call.
    Array_ReplaceRange(&arr, 0, Array_Size(&arr), NULL, 0);
    assert(Array_Size(&arr) == 0);

    Array_Free(&arr);
}

TEST(array, array_replace_range_grow_with_tail)
{
    Array arr;
    Array_InitChar(&arr, 3);
    Array_Append(&arr, "ac", 2); // size=2, capacity=3

    // Replacing 0 chars at position 1 with "XYZ" leaves a 1-char tail ("c") that
    // must survive the capacity-growth reallocation (newSize=5 > capacity=3).
    Array_ReplaceRange(&arr, 1, 0, "XYZ", 3);
    assert(Array_Size(&arr) == 5);
    assert(arr.capacity >= 5);
    Slice s = Array_ToSlice(&arr);
    assert(memcmp(s.data, "aXYZc", 5) == 0);

    Array_Free(&arr);
}

TEST(array, array_replace_range_exact_capacity_fit)
{
    Array arr;
    Array_InitChar(&arr, 4);
    Array_Append(&arr, "ab", 2); // size=2, capacity=4

    // newSize (4) exactly equals capacity (4) -- must take the non-growth path.
    size_t capacityBefore = arr.capacity;
    Array_ReplaceRange(&arr, 2, 0, "cd", 2);
    assert(arr.capacity == capacityBefore && "Exact-fit replace should not reallocate");
    assert(Array_Size(&arr) == 4);
    Slice s = Array_ToSlice(&arr);
    assert(memcmp(s.data, "abcd", 4) == 0);

    Array_Free(&arr);
}

TEST(array, array_pop_empty)
{
    Array arr;
    Array_InitChar(&arr, 4);
    assert(Array_Pop(&arr) == false && "Popping an empty array should fail, not underflow size");
    assert(Array_Size(&arr) == 0);

    Array_Free(&arr);
}

TEST(array, array_at_and_raw_at)
{
    Array arr;
    Array_InitChar(&arr, 8);
    Array_Append(&arr, "ab", 2); // size=2, capacity=8

    assert(*(char*)Array_At(&arr, 0) == 'a');
    assert(*(char*)Array_At(&arr, 1) == 'b');
    assert(Array_RawAt(&arr, 0) == Array_At(&arr, 0));

    // Array_RawAt indexes into capacity, so an index past size but within
    // capacity is valid -- unlike Array_At, which would assert on it.
    void* raw = Array_RawAt(&arr, arr.capacity - 1);
    assert(raw == (char*)arr.data + (arr.capacity - 1));

    Array_Free(&arr);
}

TEST(array, array_free_resets_state)
{
    Array arr;
    Array_InitChar(&arr, 4);
    Array_Append(&arr, "ab", 2);

    Array_Free(&arr);
    assert(arr.data == NULL);
    assert(arr.size == 0);
    assert(arr.capacity == 0);
}

TEST(array, array_init_natural_alignment)
{
    Array arr;
    // alignment=0 falls back to natural (max_align_t) alignment instead of a caller-specified one.
    Array_Init(&arr, sizeof(int), 4, 0);
    assert(arr.data != NULL);

    int values[3] = { 10, 20, 30 };
    Array_Append(&arr, values, 3);
    assert(Array_Size(&arr) == 3);
    assert(*(int*)Array_At(&arr, 0) == 10);
    assert(*(int*)Array_At(&arr, 2) == 30);

    Array_Free(&arr);
}
