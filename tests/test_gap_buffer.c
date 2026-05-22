#include "../src/neo.h"
#include <assert.h>
#include <string.h>

static void test_gap_buffer_basic(void)
{
    GapBuffer gb;
    GapBuffer_Init(&gb, 10, 64);

    assert(GapBuffer_Size(&gb) == 0 && "New gap buffer size should be 0");

    // Insert characters
    GapBuffer_InsertChar(&gb, 0, 'a');
    GapBuffer_InsertChar(&gb, 1, 'c');
    assert(GapBuffer_Size(&gb) == 2 && "Size should be 2 after inserting 'a' and 'c'");

    // Insert char in between
    GapBuffer_InsertChar(&gb, 1, 'b');
    assert(GapBuffer_Size(&gb) == 3 && "Size should be 3");

    Slice slice = GapBuffer_ToSlice(&gb);
    assert(slice.size == 3);
    assert(memcmp(slice.data, "abc", 3) == 0 && "Content should be 'abc'");

    // Insert slice
    Slice toInsert = Slice_From("def");
    GapBuffer_InsertSlice(&gb, 3, toInsert);
    assert(GapBuffer_Size(&gb) == 6 && "Size should be 6 after inserting 'def'");

    slice = GapBuffer_ToSlice(&gb);
    assert(slice.size == 6);
    assert(memcmp(slice.data, "abcdef", 6) == 0 && "Content should be 'abcdef'");

    // Delete elements
    GapBuffer_Delete(&gb, 2, 2); // delete "cd", leaving "abef"
    assert(GapBuffer_Size(&gb) == 4 && "Size should be 4 after deleting 2 characters");

    slice = GapBuffer_ToSlice(&gb);
    assert(slice.size == 4);
    assert(memcmp(slice.data, "abef", 4) == 0 && "Content should be 'abef'");

    GapBuffer_Free(&gb);
}
