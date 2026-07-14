#include "../src/neo.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

static void test_line_basic(void)
{
    Line* line = Line_New(16);
    assert(line != NULL);
    assert(Line_Length(line) == 0);
    
    Line_InsertChar(line, 0, 'H');
    Line_InsertChar(line, 1, 'i');
    assert(Line_Length(line) == 2);
    
    Slice s = Line_GetText(line);
    assert(s.size == 2);
    assert(memcmp(s.data, "Hi", 2) == 0);
    
    Line_InsertText(line, 2, " there!", 7);
    assert(Line_Length(line) == 9);
    
    s = Line_GetText(line);
    assert(s.size == 9);
    assert(memcmp(s.data, "Hi there!", 9) == 0);
    
    Line_DeleteChar(line, 0); // "i there!"
    assert(Line_Length(line) == 8);
    
    Line_DeleteText(line, 0, 2); // "there!"
    assert(Line_Length(line) == 6);
    
    s = Line_GetText(line);
    assert(s.size == 6);
    assert(memcmp(s.data, "there!", 6) == 0);
    
    Line_Free(line);
}

static void test_line_get_char(void)
{
    Line* line = Line_New(16);
    Line_InsertText(line, 0, "hello", 5);

    assert(Line_GetChar(line, 0) == 'h');
    assert(Line_GetChar(line, 4) == 'o');

    Line_Free(line);
}

static void test_line_insert_position_clamping(void)
{
    Line* line = Line_New(16);
    Line_InsertText(line, 0, "abc", 3);

    // A char-insert position past the current length clamps to an append.
    Line_InsertChar(line, 100, 'X');
    assert(Line_Length(line) == 4);
    Slice s = Line_GetText(line);
    assert(memcmp(s.data, "abcX", 4) == 0);

    // Same clamping for a text-insert.
    Line_InsertText(line, 1000, "YZ", 2);
    assert(Line_Length(line) == 6);
    s = Line_GetText(line);
    assert(memcmp(s.data, "abcXYZ", 6) == 0);

    Line_Free(line);
}

static void test_line_delete_edges(void)
{
    Line* line = Line_New(16);
    Line_InsertText(line, 0, "abc", 3);

    // Delete at the last valid index (not just position 0).
    Line_DeleteChar(line, 2);
    assert(Line_Length(line) == 2);
    Slice s = Line_GetText(line);
    assert(memcmp(s.data, "ab", 2) == 0);

    // A zero-length delete is a no-op.
    Line_DeleteText(line, 0, 0);
    assert(Line_Length(line) == 2);

    Line_DeleteText(line, 1, 1); // delete the last char
    assert(Line_Length(line) == 1);
    s = Line_GetText(line);
    assert(memcmp(s.data, "a", 1) == 0);

    Line_Free(line);
}

static void test_line_buffer_backed_delegation(void)
{
    // Exercises the line->buffer != NULL branch of Line_InsertChar/InsertText/
    // DeleteChar/DeleteText, which delegate to the owning Buffer's edit API.
    Buffer* buffer = Buffer_New();
    Line* line = Buffer_GetLine(buffer, 0);
    assert(line->buffer == buffer);

    Line_InsertText(line, 0, "hello", 5);
    line = Buffer_GetLine(buffer, 0);
    assert(Line_Length(line) == 5);
    Slice s = Line_GetText(line);
    assert(memcmp(s.data, "hello", 5) == 0);

    Line_InsertChar(line, 5, '!');
    line = Buffer_GetLine(buffer, 0);
    assert(Line_Length(line) == 6);
    s = Line_GetText(line);
    assert(memcmp(s.data, "hello!", 6) == 0);

    Line_DeleteChar(line, 0);
    line = Buffer_GetLine(buffer, 0);
    assert(Line_Length(line) == 5);
    s = Line_GetText(line);
    assert(memcmp(s.data, "ello!", 5) == 0);

    Line_DeleteText(line, 0, 4); // delete "ello"
    line = Buffer_GetLine(buffer, 0);
    assert(Line_Length(line) == 1);
    s = Line_GetText(line);
    assert(memcmp(s.data, "!", 1) == 0);

    Buffer_Free(buffer);
}

static void test_line_render_x(void)
{
    Line* line = Line_New(16);
    // "a\tbc\td"
    Line_InsertText(line, 0, "a\tbc\td", 6);
    
    // TAB_STOP is 4
    assert(Line_GetRenderX(line, 0, 4) == 0); // 'a'
    assert(Line_GetRenderX(line, 1, 4) == 1); // '\t'
    assert(Line_GetRenderX(line, 2, 4) == 4); // 'b'
    assert(Line_GetRenderX(line, 3, 4) == 5); // 'c'
    assert(Line_GetRenderX(line, 4, 4) == 6); // '\t'
    assert(Line_GetRenderX(line, 5, 4) == 8); // 'd'
    assert(Line_GetRenderX(line, 6, 4) == 9); // end

    Line_Free(line);
}

static void test_line_render_x_edges(void)
{
    // Empty line: no characters to walk, always renders at column 0.
    Line* empty = Line_New(16);
    assert(Line_GetRenderX(empty, 0, 4) == 0);
    assert(Line_GetRenderX(empty, 5, 4) == 0); // cursorX past an empty line is still 0
    Line_Free(empty);

    // cursorX past the end of the text: the loop is bounded by text.size, so it
    // must stop there rather than reading (or rendering) past the real content.
    Line* line = Line_New(16);
    Line_InsertText(line, 0, "\t\t", 2); // two tabs, TAB_STOP 4 -> columns 0..3, 4..7
    assert(Line_GetRenderX(line, 10, 4) == 8);
    Line_Free(line);
}
