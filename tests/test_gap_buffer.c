#include "../src/neo.h"
#include <assert.h>
#include <string.h>

static void test_gap_buffer_basic(void)
{
    GapBuffer gb;
    GapBuffer_Init(&gb, sizeof(char), 10, 64);

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

static void test_gap_buffer_capacity_growth(void)
{
    GapBuffer gb;
    GapBuffer_Init(&gb, sizeof(char), 4, 64); // Small initial gap so it's forced to grow.
    size_t initialCapacity = gb.data.capacity;

    char expected[100];
    for (int i = 0; i < 100; i++) {
        char c = (char)('A' + (i % 26));
        GapBuffer_InsertChar(&gb, (size_t)i, c);
        expected[i] = c;
    }

    assert(GapBuffer_Size(&gb) == 100);
    assert(gb.data.capacity > initialCapacity && "Capacity should have grown past the initial small gap");

    Slice slice = GapBuffer_ToSlice(&gb);
    assert(slice.size == 100);
    assert(memcmp(slice.data, expected, 100) == 0 && "Content must survive the capacity-growth reallocation");

    GapBuffer_Free(&gb);
}

static void test_gap_buffer_move_gap_edges(void)
{
    GapBuffer gb;
    GapBuffer_Init(&gb, sizeof(char), 10, 64);
    for (size_t i = 0; i < 6; i++) {
        GapBuffer_InsertChar(&gb, i, (char)('a' + i)); // "abcdef", gapStart ends at 6
    }

    // Move the gap all the way to the start -- every index is now on the "at or
    // after gapStart" side of GapBuffer_At, the opposite branch from how these
    // chars were originally read right after insertion.
    GapBuffer_MoveGap(&gb, 0);
    const char* expected = "abcdef";
    for (size_t i = 0; i < 6; i++) {
        assert(*(char*)GapBuffer_At(&gb, i) == expected[i]);
    }

    // Move it back to the end.
    GapBuffer_MoveGap(&gb, 6);
    for (size_t i = 0; i < 6; i++) {
        assert(*(char*)GapBuffer_At(&gb, i) == expected[i]);
    }

    // No-op move (newGapStart == gapStart already) must not corrupt anything.
    GapBuffer_MoveGap(&gb, 6);
    Slice slice = GapBuffer_ToSlice(&gb);
    assert(slice.size == 6 && memcmp(slice.data, expected, 6) == 0);

    GapBuffer_Free(&gb);
}

static void test_gap_buffer_zero_size_noops(void)
{
    GapBuffer gb;
    GapBuffer_Init(&gb, sizeof(char), 10, 64);
    GapBuffer_InsertSlice(&gb, 0, Slice_From("abc"));
    assert(GapBuffer_Size(&gb) == 3);

    // Zero-size insert/delete must be no-ops.
    GapBuffer_InsertSlice(&gb, 1, Slice_Make(NULL, 0));
    assert(GapBuffer_Size(&gb) == 3);

    GapBuffer_Delete(&gb, 1, 0);
    assert(GapBuffer_Size(&gb) == 3);

    Slice slice = GapBuffer_ToSlice(&gb);
    assert(slice.size == 3 && memcmp(slice.data, "abc", 3) == 0);

    GapBuffer_Free(&gb);
}

static void test_gap_buffer_clear(void)
{
    GapBuffer gb;
    GapBuffer_Init(&gb, sizeof(char), 10, 64);
    GapBuffer_InsertSlice(&gb, 0, Slice_From("hello"));
    assert(GapBuffer_Size(&gb) == 5);

    GapBuffer_Clear(&gb);
    assert(GapBuffer_Size(&gb) == 0);
    assert(gb.gapStart == 0);
    assert(gb.gapEnd == gb.data.capacity);

    // The gap buffer must still be usable after clearing.
    GapBuffer_InsertSlice(&gb, 0, Slice_From("hi"));
    assert(GapBuffer_Size(&gb) == 2);
    Slice slice = GapBuffer_ToSlice(&gb);
    assert(slice.size == 2 && memcmp(slice.data, "hi", 2) == 0);

    GapBuffer_Free(&gb);
}

static void test_gap_buffer_to_slice_after_delete_mid_buffer(void)
{
    GapBuffer gb;
    GapBuffer_Init(&gb, sizeof(char), 10, 64);
    GapBuffer_InsertSlice(&gb, 0, Slice_From("abcdef"));

    // Deleting in the middle leaves the gap there; ToSlice must correctly
    // collapse it a second time (it was already collapsed once by the insert
    // above via GapBuffer_MoveGap's internal bookkeeping).
    GapBuffer_Delete(&gb, 2, 2); // remove "cd" -> "abef"
    Slice slice = GapBuffer_ToSlice(&gb);
    assert(slice.size == 4 && memcmp(slice.data, "abef", 4) == 0);

    // A second ToSlice call right after (gap already collapsed at data.size) must
    // still produce the same correct result.
    slice = GapBuffer_ToSlice(&gb);
    assert(slice.size == 4 && memcmp(slice.data, "abef", 4) == 0);

    GapBuffer_Free(&gb);
}
