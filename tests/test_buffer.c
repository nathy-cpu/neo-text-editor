#include "../src/neo.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static void test_buffer_basic(void)
{
    Buffer* buffer = Buffer_New();
    assert(buffer != NULL);
    assert(buffer->mappedFile.fileDescriptor == -1);
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
    assert(Tab_GetGutterWidth(&tab) == 7);
    
    // Add lines up to 9
    for (size_t i = 1; i < 9; i++) {
        Buffer_InsertLine(tab.buffer, i);
    }
    assert(Buffer_GetLineCount(tab.buffer) == 9);
    assert(Tab_GetGutterDigits(&tab) == 3);
    assert(Tab_GetGutterWidth(&tab) == 7);

    // Add 1 more line (10 lines total)
    Buffer_InsertLine(tab.buffer, 9);
    assert(Buffer_GetLineCount(tab.buffer) == 10);
    assert(Tab_GetGutterDigits(&tab) == 3);
    assert(Tab_GetGutterWidth(&tab) == 7);

    // Add lines up to 100
    for (size_t i = 10; i < 100; i++) {
        Buffer_InsertLine(tab.buffer, i);
    }
    assert(Buffer_GetLineCount(tab.buffer) == 100);
    assert(Tab_GetGutterDigits(&tab) == 3);
    assert(Tab_GetGutterWidth(&tab) == 7);

    // Add lines up to 1000
    for (size_t i = 100; i < 1000; i++) {
        Buffer_InsertLine(tab.buffer, i);
    }
    assert(Buffer_GetLineCount(tab.buffer) == 1000);
    assert(Tab_GetGutterDigits(&tab) == 4); // 1000 is 4 digits
    assert(Tab_GetGutterWidth(&tab) == 8);

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

static void test_tab_folding(void)
{
    Tab tab;
    Tab_Init(&tab);

    Line* line0 = Buffer_GetLine(tab.buffer, 0);
    Line_InsertText(line0, 0, "if (cond) {", 11);

    Buffer_InsertLine(tab.buffer, 1);
    Line* line1 = Buffer_GetLine(tab.buffer, 1);
    Line_InsertText(line1, 0, "    foo();", 10);

    Buffer_InsertLine(tab.buffer, 2);
    Line* line2 = Buffer_GetLine(tab.buffer, 2);
    Line_InsertText(line2, 0, "    bar();", 10);

    Buffer_InsertLine(tab.buffer, 3);
    Line* line3 = Buffer_GetLine(tab.buffer, 3);
    Line_InsertText(line3, 0, "}", 1);

    Buffer_InsertLine(tab.buffer, 4);
    Line* line4 = Buffer_GetLine(tab.buffer, 4);
    Line_InsertText(line4, 0, "", 0);

    Buffer_InsertLine(tab.buffer, 5);
    Line* line5 = Buffer_GetLine(tab.buffer, 5);
    Line_InsertText(line5, 0, "else {", 6);

    Buffer_InsertLine(tab.buffer, 6);
    Line* line6 = Buffer_GetLine(tab.buffer, 6);
    Line_InsertText(line6, 0, "    baz();", 10);

    Buffer_InsertLine(tab.buffer, 7);
    Line* line7 = Buffer_GetLine(tab.buffer, 7);
    Line_InsertText(line7, 0, "}", 1);

    assert(Line_GetIndentation(line0, 4) == 0);
    assert(Line_GetIndentation(line1, 4) == 4);
    assert(Line_IsBlank(line4) == true);
    assert(Line_IsBlank(line0) == false);

    assert(Line_IsFoldable(tab.buffer, 0, 4) == true);
    assert(Line_IsFoldable(tab.buffer, 1, 4) == false);
    assert(Line_IsFoldable(tab.buffer, 3, 4) == false);

    Tab_UpdateVisualRows((const Editor*)NULL, &tab, 80);
    assert(Array_Size(&tab.visualRows) == 8);

    for (size_t i = 0; i < 8; i++) {
        assert(Tab_IsLineVisible(&tab, i) == true);
    }

    line0->isFolded = true;
    Tab_UpdateVisualRows((const Editor*)NULL, &tab, 80);

    assert(Array_Size(&tab.visualRows) == 6);

    assert(Tab_IsLineVisible(&tab, 0) == true);
    assert(Tab_IsLineVisible(&tab, 1) == false);
    assert(Tab_IsLineVisible(&tab, 2) == false);
    assert(Tab_IsLineVisible(&tab, 3) == true);
    assert(Tab_IsLineVisible(&tab, 4) == true);
    assert(Tab_IsLineVisible(&tab, 5) == true);
    assert(Tab_IsLineVisible(&tab, 6) == true);
    assert(Tab_IsLineVisible(&tab, 7) == true);

    assert(Tab_NextVisibleLine(&tab, 0) == 3);
    assert(Tab_PrevVisibleLine(&tab, 3) == 0);

    Tab_Free(&tab);
}

static void test_editor_toggle_all_folds(void)
{
    Editor editor;
    Editor_Init(&editor);

    Editor_AddTab(&editor, NULL);
    Tab* tab = Array_Get(&editor.tabs, Tab*, editor.activeTabIndex);

    Line* line0 = Buffer_GetLine(tab->buffer, 0);
    Line_InsertText(line0, 0, "if (cond) {", 11);

    Buffer_InsertLine(tab->buffer, 1);
    Line* line1 = Buffer_GetLine(tab->buffer, 1);
    Line_InsertText(line1, 0, "    foo();", 10);

    Buffer_InsertLine(tab->buffer, 2);
    Line* line2 = Buffer_GetLine(tab->buffer, 2);
    Line_InsertText(line2, 0, "    bar();", 10);

    Buffer_InsertLine(tab->buffer, 3);
    Line* line3 = Buffer_GetLine(tab->buffer, 3);
    Line_InsertText(line3, 0, "}", 1);

    Buffer_InsertLine(tab->buffer, 4);
    Line* line4 = Buffer_GetLine(tab->buffer, 4);
    Line_InsertText(line4, 0, "else {", 6);

    Buffer_InsertLine(tab->buffer, 5);
    Line* line5 = Buffer_GetLine(tab->buffer, 5);
    Line_InsertText(line5, 0, "    baz();", 10);

    Buffer_InsertLine(tab->buffer, 6);
    Line* line6 = Buffer_GetLine(tab->buffer, 6);
    Line_InsertText(line6, 0, "}", 1);

    assert(Line_IsFoldable(tab->buffer, 0, 4) == true);
    assert(Line_IsFoldable(tab->buffer, 4, 4) == true);
    assert(Line_IsFoldable(tab->buffer, 1, 4) == false);

    assert(line0->isFolded == false);
    assert(line4->isFolded == false);

    Editor_ToggleAllFolds(&editor);
    assert(line0->isFolded == true);
    assert(line4->isFolded == true);

    Editor_ToggleAllFolds(&editor);
    assert(line0->isFolded == false);
    assert(line4->isFolded == false);

    Editor_Free(&editor);
}

static void test_buffer_piece_table(void)
{
    const char* path = "test_temp_piece_table.txt";
    Slice content = Slice_Make("Hello, World!\nWelcome to Neo!\n", 30);
    assert(FileIoWrite(path, content));

    MappedFile mapped = FileIoMmap(path);
    assert(mapped.fileDescriptor != -1);

    Buffer* buffer = Buffer_NewFromMmap(mapped, path);
    assert(buffer != NULL);
    assert(Buffer_GetLineCount(buffer) == 3);

    Line* line0 = Buffer_GetLine(buffer, 0);
    assert(line0 != NULL);
    Slice l0Text = Line_GetText(line0);
    assert(l0Text.size == 13);
    assert(memcmp(l0Text.data, "Hello, World!", 13) == 0);

    Buffer_InsertText(buffer, 7, "Beautiful ", 10);
    
    line0 = Buffer_GetLine(buffer, 0);
    l0Text = Line_GetText(line0);
    assert(l0Text.size == 23);
    assert(memcmp(l0Text.data, "Hello, Beautiful World!", 23) == 0);
    assert(Buffer_GetTotalBytes(buffer) == 40);

    Buffer_DeleteRange(buffer, 7, 17);
    line0 = Buffer_GetLine(buffer, 0);
    l0Text = Line_GetText(line0);
    assert(l0Text.size == 13);
    assert(memcmp(l0Text.data, "Hello, World!", 13) == 0);

    Buffer_OnSave(buffer, path);
    assert(buffer->isModified == false);
    assert(Buffer_GetLineCount(buffer) == 3);

    Buffer_Free(buffer);
    remove(path);
}

