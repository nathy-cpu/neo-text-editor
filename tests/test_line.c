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

static void test_line_render_x(void)
{
    Line* line = Line_New(16);
    // "a\tbc\td"
    Line_InsertText(line, 0, "a\tbc\td", 6);
    
    // TAB_STOP is 4
    assert(Line_GetRenderX(line, 0) == 0); // 'a'
    assert(Line_GetRenderX(line, 1) == 1); // '\t'
    assert(Line_GetRenderX(line, 2) == 4); // 'b'
    assert(Line_GetRenderX(line, 3) == 5); // 'c'
    assert(Line_GetRenderX(line, 4) == 6); // '\t'
    assert(Line_GetRenderX(line, 5) == 8); // 'd'
    assert(Line_GetRenderX(line, 6) == 9); // end
    
    Line_Free(line);
}
