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
