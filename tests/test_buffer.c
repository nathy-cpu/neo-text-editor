#include "../src/neo.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static void test_buffer_basic(void)
{
    Buffer* buffer = Buffer_New();
    assert(buffer != NULL);
    assert(Buffer_GetLineCount(buffer) == 1);
    
    Buffer_InsertChar(buffer, 0, 0, 'a');
    Buffer_InsertChar(buffer, 0, 1, 'b');
    Buffer_InsertChar(buffer, 0, 2, 'c');
    
    Line* line = Buffer_GetLine(buffer, 0);
    assert(Line_Length(line) == 3);
    
    Buffer_SplitLine(buffer, 0, 1); // "a", "bc"
    assert(Buffer_GetLineCount(buffer) == 2);
    
    line = Buffer_GetLine(buffer, 0);
    assert(Line_Length(line) == 1);
    
    line = Buffer_GetLine(buffer, 1);
    assert(Line_Length(line) == 2);
    
    Buffer_JoinLine(buffer, 0); // "abc"
    assert(Buffer_GetLineCount(buffer) == 1);
    
    line = Buffer_GetLine(buffer, 0);
    assert(Line_Length(line) == 3);
    
    Buffer_Free(buffer);
}

static void test_buffer_lines(void)
{
    Buffer* buffer = Buffer_New();
    
    Buffer_InsertLine(buffer, 1);
    Buffer_InsertLine(buffer, 2);
    assert(Buffer_GetLineCount(buffer) == 3);
    
    Buffer_InsertChar(buffer, 0, 0, '1');
    Buffer_InsertChar(buffer, 1, 0, '2');
    Buffer_InsertChar(buffer, 2, 0, '3');
    
    Buffer_DeleteLine(buffer, 1);
    assert(Buffer_GetLineCount(buffer) == 2);
    
    Line* line = Buffer_GetLine(buffer, 1);
    Slice text = Line_GetText(line);
    assert(text.size == 1 && ((const char*)text.data)[0] == '3');
    
    Buffer_Free(buffer);
}

static void test_tab_gutter(void)
{
    Tab tab;
    Tab_Init(&tab);
    
    // Initial buffer has 1 line
    assert(Tab_GetGutterDigits(&tab) == 3);
    assert(Tab_GetGutterWidth(&tab) == 6);
    
    // Add lines up to 9
    for (size_t i = 1; i < 9; i++) {
        Buffer_InsertLine(tab.buffer, i);
    }
    assert(Buffer_GetLineCount(tab.buffer) == 9);
    assert(Tab_GetGutterDigits(&tab) == 3);
    assert(Tab_GetGutterWidth(&tab) == 6);

    // Add 1 more line (10 lines total)
    Buffer_InsertLine(tab.buffer, 9);
    assert(Buffer_GetLineCount(tab.buffer) == 10);
    assert(Tab_GetGutterDigits(&tab) == 3);
    assert(Tab_GetGutterWidth(&tab) == 6);

    // Add lines up to 100
    for (size_t i = 10; i < 100; i++) {
        Buffer_InsertLine(tab.buffer, i);
    }
    assert(Buffer_GetLineCount(tab.buffer) == 100);
    assert(Tab_GetGutterDigits(&tab) == 3);
    assert(Tab_GetGutterWidth(&tab) == 6);

    // Add lines up to 1000
    for (size_t i = 100; i < 1000; i++) {
        Buffer_InsertLine(tab.buffer, i);
    }
    assert(Buffer_GetLineCount(tab.buffer) == 1000);
    assert(Tab_GetGutterDigits(&tab) == 4); // 1000 is 4 digits
    assert(Tab_GetGutterWidth(&tab) == 7);

    Tab_Free(&tab);
}

static void test_tab_wrapping(void)
{
    Tab tab;
    Tab_Init(&tab);
    
    // Add text to the first line
    Line* line = Buffer_GetLine(tab.buffer, 0);
    // Insert "abcdef" (length 6)
    Line_InsertText(line, 0, "abcdef", 6);
    
    // Usable columns: 3
    Tab_UpdateVisualRows((const Editor*)NULL, &tab, 3);
    
    // Expecting 2 visual rows: "abc" and "def"
    size_t size = Array_Size(&tab.visualRows);
    assert(size == 2);
    
    VisualRow* vr0 = (VisualRow*)Array_At(&tab.visualRows, 0);
    assert(vr0->lineIndex == 0);
    assert(vr0->startCol == 0);
    assert(vr0->length == 3);
    assert(vr0->isWrapped == false);

    VisualRow* vr1 = (VisualRow*)Array_At(&tab.visualRows, 1);
    assert(vr1->lineIndex == 0);
    assert(vr1->startCol == 3);
    assert(vr1->length == 3);
    assert(vr1->isWrapped == true);
    
    // Verify mapping from logical cursor to visual row
    tab.cursorY = 0;
    tab.cursorX = 0;
    assert(Tab_GetCursorVRowIdx(&tab) == 0);
    assert(Tab_GetCursorVisualCol(&tab, 0) == 0);

    tab.cursorX = 2;
    assert(Tab_GetCursorVRowIdx(&tab) == 0);
    assert(Tab_GetCursorVisualCol(&tab, 0) == 2);

    tab.cursorX = 3;
    assert(Tab_GetCursorVRowIdx(&tab) == 1);
    assert(Tab_GetCursorVisualCol(&tab, 1) == 0);

    tab.cursorX = 5;
    assert(Tab_GetCursorVRowIdx(&tab) == 1);
    assert(Tab_GetCursorVisualCol(&tab, 1) == 2);

    tab.cursorX = 6;
    assert(Tab_GetCursorVRowIdx(&tab) == 1);
    assert(Tab_GetCursorVisualCol(&tab, 1) == 3);

    // Test cursor movements setting logical coordinates
    // Setting visual row 0, col 2 -> cursorX = 2
    Tab_SetCursorFromVRow(&tab, 0, 2);
    assert(tab.cursorY == 0);
    assert(tab.cursorX == 2);

    // Setting visual row 1, col 1 -> cursorX = 4
    Tab_SetCursorFromVRow(&tab, 1, 1);
    assert(tab.cursorY == 0);
    assert(tab.cursorX == 4);

    Tab_Free(&tab);
}
