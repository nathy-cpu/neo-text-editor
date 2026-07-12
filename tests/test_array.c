#include "../src/neo.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

static void test_array_basic(void)
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

static void test_array_growth(void)
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

static void test_array_replace_range_shrink(void)
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

static void test_array_replace_range_grow(void)
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

static void test_array_replace_range_ends(void)
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
