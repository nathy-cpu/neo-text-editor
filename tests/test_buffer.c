#include "../src/core/buffer.h"
#include "../src/core/line.h"
#include "../src/features/tab.h"
#include "../src/features/editor.h"
#include "../src/utils/file_io.h"
#include "../src/utils/slice.h"
#include "../src/utils/array.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

TEST(buffer, buffer_basic)
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

TEST(buffer, buffer_lines)
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
    Slice text = Line_GetText(line, buffer);
    assert(text.size == 1 && ((const char*)text.data)[0] == '3');
    
    Buffer_Free(buffer);
}

TEST(buffer, tab_gutter)
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

TEST(buffer, tab_wrapping)
{
    Tab tab;
    Tab_Init(&tab);
    
    // Add text to the first line
    Line* line = Buffer_GetLine(tab.buffer, 0);
    // Insert "abcdef" (length 6)
    Buffer_InsertText(tab.buffer, line->offset, "abcdef", 6);
    
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

TEST(buffer, tab_folding)
{
    Tab tab;
    Tab_Init(&tab);

    Line* line0 = Buffer_GetLine(tab.buffer, 0);
    Buffer_InsertText(tab.buffer, line0->offset, "if (cond) {", 11);

    Buffer_InsertLine(tab.buffer, 1);
    Line* line1 = Buffer_GetLine(tab.buffer, 1);
    Buffer_InsertText(tab.buffer, line1->offset, "    foo();", 10);

    Buffer_InsertLine(tab.buffer, 2);
    Line* line2 = Buffer_GetLine(tab.buffer, 2);
    Buffer_InsertText(tab.buffer, line2->offset, "    bar();", 10);

    Buffer_InsertLine(tab.buffer, 3);
    Line* line3 = Buffer_GetLine(tab.buffer, 3);
    Buffer_InsertText(tab.buffer, line3->offset, "}", 1);

    Buffer_InsertLine(tab.buffer, 4);
    Line* line4 = Buffer_GetLine(tab.buffer, 4);
    Buffer_InsertText(tab.buffer, line4->offset, "", 0);

    Buffer_InsertLine(tab.buffer, 5);
    Line* line5 = Buffer_GetLine(tab.buffer, 5);
    Buffer_InsertText(tab.buffer, line5->offset, "else {", 6);

    Buffer_InsertLine(tab.buffer, 6);
    Line* line6 = Buffer_GetLine(tab.buffer, 6);
    Buffer_InsertText(tab.buffer, line6->offset, "    baz();", 10);

    Buffer_InsertLine(tab.buffer, 7);
    Line* line7 = Buffer_GetLine(tab.buffer, 7);
    Buffer_InsertText(tab.buffer, line7->offset, "}", 1);

    assert(Line_GetIndentation(line0, tab.buffer, 4) == 0);
    assert(Line_GetIndentation(line1, tab.buffer, 4) == 4);
    assert(Line_IsBlank(line4, tab.buffer) == true);
    assert(Line_IsBlank(line0, tab.buffer) == false);

    assert(Line_IsFoldable(tab.buffer, 0, 4) == true);
    assert(Line_IsFoldable(tab.buffer, 1, 4) == false);
    assert(Line_IsFoldable(tab.buffer, 3, 4) == false);

    Tab_UpdateVisualRows((const Editor*)NULL, &tab, 80);
    assert(Array_Size(&tab.visualRows) == 8);

    for (size_t i = 0; i < 8; i++) {
        assert(Tab_IsLineVisible(&tab, i) == true);
    }

    line0->isFolded = true;
    tab.buffer->foldedLineCount = 1;
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

TEST(buffer, editor_toggle_all_folds)
{
    Editor editor;
    Editor_Init(&editor);

    Editor_AddTab(&editor, NULL);
    Tab* tab = Array_Get(&editor.tabs, Tab*, editor.activeTabIndex);

    Line* line0 = Buffer_GetLine(tab->buffer, 0);
    Buffer_InsertText(tab->buffer, line0->offset, "if (cond) {", 11);

    Buffer_InsertLine(tab->buffer, 1);
    Line* line1 = Buffer_GetLine(tab->buffer, 1);
    Buffer_InsertText(tab->buffer, line1->offset, "    foo();", 10);

    Buffer_InsertLine(tab->buffer, 2);
    Line* line2 = Buffer_GetLine(tab->buffer, 2);
    Buffer_InsertText(tab->buffer, line2->offset, "    bar();", 10);

    Buffer_InsertLine(tab->buffer, 3);
    Line* line3 = Buffer_GetLine(tab->buffer, 3);
    Buffer_InsertText(tab->buffer, line3->offset, "}", 1);

    Buffer_InsertLine(tab->buffer, 4);
    Line* line4 = Buffer_GetLine(tab->buffer, 4);
    Buffer_InsertText(tab->buffer, line4->offset, "else {", 6);

    Buffer_InsertLine(tab->buffer, 5);
    Line* line5 = Buffer_GetLine(tab->buffer, 5);
    Buffer_InsertText(tab->buffer, line5->offset, "    baz();", 10);

    Buffer_InsertLine(tab->buffer, 6);
    Line* line6 = Buffer_GetLine(tab->buffer, 6);
    Buffer_InsertText(tab->buffer, line6->offset, "}", 1);

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

// Regression test for the perf fix in Buffer_RebuildLineCache: editing a
// single line deep inside a multi-line buffer must only invalidate that one
// line (the "aligned" fast path), not rebuild the whole rest of the file.
// Only when two edits land back-to-back with no intervening rebuild (so the
// realignment offset can no longer be trusted) should the full tail rebuild.
TEST(buffer, buffer_last_rebuild_range)
{
    Buffer* buffer = Buffer_New();
    for (size_t i = 1; i < 5; i++) {
        Buffer_InsertLine(buffer, i);
    }
    for (size_t i = 0; i < 5; i++) {
        Buffer_InsertChar(buffer, i, 0, (char)('0' + i));
    }
    assert(Buffer_GetLineCount(buffer) == 5);

    // Aligned case: a single edit on line 2 only invalidates line 2.
    Buffer_InsertChar(buffer, 2, 1, 'x'); // line 2: "2" -> "2x"
    assert(Buffer_GetLineCount(buffer) == 5);

    size_t oldStart = SIZE_MAX, oldEnd = SIZE_MAX, newEnd = SIZE_MAX;
    assert(Buffer_GetLastRebuildRange(buffer, &oldStart, &oldEnd, &newEnd) == true);
    assert(oldStart == 2);
    assert(oldEnd == 3);
    assert(newEnd == 3);

    // Unaligned case: two direct edits on different lines with no rebuild in
    // between merge into one dirty range and disable the realignment offset,
    // forcing the whole tail (line 0 through the end) to be rebuilt.
    Buffer_InsertText(buffer, 0, "y", 1); // line 0: "0" -> "y0"
    Buffer_InsertText(buffer, 8, "z", 1); // line 3: "3" -> "z3"
    assert(Buffer_GetLineCount(buffer) == 5);

    Line* line0 = Buffer_GetLine(buffer, 0);
    Slice l0Text = Line_GetText(line0, buffer);
    assert(l0Text.size == 2 && memcmp(l0Text.data, "y0", 2) == 0);
    Line* line3 = Buffer_GetLine(buffer, 3);
    Slice l3Text = Line_GetText(line3, buffer);
    assert(l3Text.size == 2 && memcmp(l3Text.data, "z3", 2) == 0);

    assert(Buffer_GetLastRebuildRange(buffer, &oldStart, &oldEnd, &newEnd) == true);
    assert(oldStart == 0);
    assert(oldEnd == 5);
    assert(newEnd == 5);

    Buffer_Free(buffer);
}

// Regression test for the perf fix in Tab_UpdateVisualRows: an edit that
// doesn't change the total line count should only re-wrap the edited line
// (no renumbering of the rest), and an edit that does change the line count
// should shift only the lineIndex of rows after the edit, not rebuild/move
// the rows before it.
TEST(buffer, tab_visual_rows_incremental)
{
    Tab tab;
    Tab_Init(&tab);

    for (size_t i = 1; i < 6; i++) {
        Buffer_InsertLine(tab.buffer, i);
    }
    for (size_t i = 0; i < 6; i++) {
        char text[8];
        int n = snprintf(text, sizeof(text), "L%zu", i);
        Line* line = Buffer_GetLine(tab.buffer, i);
        Buffer_InsertText(tab.buffer, line->offset, text, (size_t)n);
    }
    assert(Buffer_GetLineCount(tab.buffer) == 6);

    size_t usableColumns = 10;
    Tab_UpdateVisualRows((const Editor*)NULL, &tab, usableColumns);
    assert(Array_Size(&tab.visualRows) == 6);
    for (size_t i = 0; i < 6; i++) {
        VisualRow* vr = (VisualRow*)Array_At(&tab.visualRows, i);
        assert(vr->lineIndex == i);
        assert(vr->isWrapped == false);
    }

    // Extend line 1 well past usableColumns so it now wraps into 2 rows.
    // Total line count is unchanged (no newline inserted).
    Line* line1 = Buffer_GetLine(tab.buffer, 1);
    Buffer_InsertText(tab.buffer, line1->offset + line1->length, "0123456789ABCDE", 15);
    assert(Buffer_GetLineCount(tab.buffer) == 6);

    Tab_UpdateVisualRows((const Editor*)NULL, &tab, usableColumns);
    // line1 is now "L10123456789ABCDE" (17 chars) at width 10 -> wraps into 2 rows.
    assert(Array_Size(&tab.visualRows) == 7);

    VisualRow* vr = (VisualRow*)Array_At(&tab.visualRows, 0);
    assert(vr->lineIndex == 0 && vr->length == 2); // "L0" untouched

    vr = (VisualRow*)Array_At(&tab.visualRows, 1);
    assert(vr->lineIndex == 1 && vr->startCol == 0 && vr->length == 10 && vr->isWrapped == false);

    vr = (VisualRow*)Array_At(&tab.visualRows, 2);
    assert(vr->lineIndex == 1 && vr->startCol == 10 && vr->length == 7 && vr->isWrapped == true);

    // Lines 2..5 must be untouched and NOT renumbered (line count didn't change).
    for (size_t i = 2; i < 6; i++) {
        vr = (VisualRow*)Array_At(&tab.visualRows, i + 1);
        assert(vr->lineIndex == i);
    }

    // Now split line 2 into two lines -- a line-count-changing edit -- and
    // confirm rows after the split point get renumbered (shifted by +1).
    Buffer_SplitLine(tab.buffer, 2, 1); // "L2" -> "L", "2"
    assert(Buffer_GetLineCount(tab.buffer) == 7);

    Tab_UpdateVisualRows((const Editor*)NULL, &tab, usableColumns);
    assert(Array_Size(&tab.visualRows) == 8);

    // Rows before the split point (line 0, line 1's two segments) are untouched.
    vr = (VisualRow*)Array_At(&tab.visualRows, 0);
    assert(vr->lineIndex == 0);
    vr = (VisualRow*)Array_At(&tab.visualRows, 1);
    assert(vr->lineIndex == 1 && vr->isWrapped == false);
    vr = (VisualRow*)Array_At(&tab.visualRows, 2);
    assert(vr->lineIndex == 1 && vr->isWrapped == true);

    // Old line 2's content, now split into logical lines 2 and 3.
    vr = (VisualRow*)Array_At(&tab.visualRows, 3);
    assert(vr->lineIndex == 2);
    vr = (VisualRow*)Array_At(&tab.visualRows, 4);
    assert(vr->lineIndex == 3);

    // Old lines 3,4,5 are now logical lines 4,5,6 -- confirm the shift.
    for (size_t i = 0; i < 3; i++) {
        vr = (VisualRow*)Array_At(&tab.visualRows, 5 + i);
        assert(vr->lineIndex == 4 + i);
    }

    Tab_Free(&tab);
}

// Regression test: a single line longer than LINE_HUGE_THRESHOLD must be
// treated as one unwrapped visual row even when the tab's word-wrap setting
// is on, avoiding an O(length) tab-stop walk and a huge VisualRow array for
// that one line. Neighboring normal-sized lines must still wrap normally.
TEST(buffer, tab_visual_rows_huge_line_bypasses_wrap)
{
    Tab tab;
    Tab_Init(&tab);
    assert(tab.config->wrapLines == true);

    Buffer_InsertLine(tab.buffer, 1);
    Buffer_InsertLine(tab.buffer, 2);

    Line* line0 = Buffer_GetLine(tab.buffer, 0);
    Buffer_InsertText(tab.buffer, line0->offset, "0123456789ABCDEFGHIJ", 20); // 20 chars, wraps at width 10

    size_t hugeLength = LINE_HUGE_THRESHOLD + 100;
    char* hugeContent = malloc(hugeLength);
    assert(hugeContent);
    memset(hugeContent, 'a', hugeLength);
    Line* line1 = Buffer_GetLine(tab.buffer, 1);
    Buffer_InsertText(tab.buffer, line1->offset, hugeContent, hugeLength);
    free(hugeContent);

    Line* line2 = Buffer_GetLine(tab.buffer, 2);
    Buffer_InsertText(tab.buffer, line2->offset, "0123456789ABCDEFGHIJ", 20);

    assert(Buffer_GetLineCount(tab.buffer) == 3);

    size_t usableColumns = 10;
    Tab_UpdateVisualRows((const Editor*)NULL, &tab, usableColumns);

    // Line 0 (20 chars, width 10) wraps into 2 rows as usual.
    size_t rowIdx = 0;
    VisualRow* vr = (VisualRow*)Array_At(&tab.visualRows, rowIdx++);
    assert(vr->lineIndex == 0 && vr->isWrapped == false);
    vr = (VisualRow*)Array_At(&tab.visualRows, rowIdx++);
    assert(vr->lineIndex == 0 && vr->isWrapped == true);

    // The huge line collapses to exactly one unwrapped row covering its full length.
    vr = (VisualRow*)Array_At(&tab.visualRows, rowIdx++);
    assert(vr->lineIndex == 1 && vr->startCol == 0 && vr->length == hugeLength && vr->isWrapped == false);

    // Line 2 wraps normally again, unaffected by the huge line before it.
    vr = (VisualRow*)Array_At(&tab.visualRows, rowIdx++);
    assert(vr->lineIndex == 2 && vr->isWrapped == false);
    vr = (VisualRow*)Array_At(&tab.visualRows, rowIdx++);
    assert(vr->lineIndex == 2 && vr->isWrapped == true);

    assert(Array_Size(&tab.visualRows) == rowIdx);

    Tab_Free(&tab);
}

TEST(buffer, buffer_piece_table)
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
    Slice l0Text = Line_GetText(line0, buffer);
    assert(l0Text.size == 13);
    assert(memcmp(l0Text.data, "Hello, World!", 13) == 0);

    Buffer_InsertText(buffer, 7, "Beautiful ", 10);
    
    line0 = Buffer_GetLine(buffer, 0);
    l0Text = Line_GetText(line0, buffer);
    assert(l0Text.size == 23);
    assert(memcmp(l0Text.data, "Hello, Beautiful World!", 23) == 0);
    assert(Buffer_GetTotalBytes(buffer) == 40);

    Buffer_DeleteRange(buffer, 7, 17);
    line0 = Buffer_GetLine(buffer, 0);
    l0Text = Line_GetText(line0, buffer);
    assert(l0Text.size == 13);
    assert(memcmp(l0Text.data, "Hello, World!", 13) == 0);

    Buffer_OnSave(buffer, path, true);
    assert(buffer->isModified == false);
    assert(Buffer_GetLineCount(buffer) == 3);

    Buffer_Free(buffer);
    remove(path);
}

TEST(buffer, buffer_undo_redo_insert_char)
{
    Buffer* buffer = Buffer_New();

    Buffer_InsertChar(buffer, 0, 0, 'a');
    Buffer_InsertChar(buffer, 0, 1, 'b');
    Buffer_InsertChar(buffer, 0, 2, 'c');

    Line* line = Buffer_GetLine(buffer, 0);
    Slice text = Line_GetText(line, buffer);
    assert(text.size == 3 && memcmp(text.data, "abc", 3) == 0);

    // Adjacent char inserts coalesce into a single ActionGroup, so one undo
    // reverts all three at once.
    buffer->isModified = false;
    size_t undoLine = SIZE_MAX, undoColumn = SIZE_MAX;
    assert(Buffer_Undo(buffer, &undoLine, &undoColumn) == true);
    assert(buffer->isModified == true);
    line = Buffer_GetLine(buffer, 0);
    assert(Line_Length(line) == 0);
    assert(undoLine == 0 && undoColumn == 0);

    size_t redoLine = SIZE_MAX, redoColumn = SIZE_MAX;
    buffer->isModified = false;
    assert(Buffer_Redo(buffer, &redoLine, &redoColumn) == true);
    assert(buffer->isModified == true);
    line = Buffer_GetLine(buffer, 0);
    text = Line_GetText(line, buffer);
    assert(text.size == 3 && memcmp(text.data, "abc", 3) == 0);
    assert(redoLine == 0 && redoColumn == 3);

    Buffer_Free(buffer);
}

TEST(buffer, buffer_undo_redo_grouping_boundary)
{
    Buffer* buffer = Buffer_New();
    Buffer_InsertLine(buffer, 1);

    Buffer_InsertChar(buffer, 0, 0, 'a');
    Buffer_InsertChar(buffer, 1, 0, 'b'); // Different line -- forces a new group.

    Line* line0 = Buffer_GetLine(buffer, 0);
    Line* line1 = Buffer_GetLine(buffer, 1);
    assert(Line_Length(line0) == 1);
    assert(Line_Length(line1) == 1);

    size_t l = SIZE_MAX, c = SIZE_MAX;
    assert(Buffer_Undo(buffer, &l, &c) == true);
    line0 = Buffer_GetLine(buffer, 0);
    line1 = Buffer_GetLine(buffer, 1);
    assert(Line_Length(line0) == 1); // Untouched -- only the line1 group reverted.
    assert(Line_Length(line1) == 0);

    assert(Buffer_Undo(buffer, &l, &c) == true);
    line0 = Buffer_GetLine(buffer, 0);
    assert(Line_Length(line0) == 0);

    Buffer_Free(buffer);
}

TEST(buffer, buffer_undo_redo_delete_split_join)
{
    // ACTION_DELETE_CHAR
    {
        Buffer* buffer = Buffer_New();
        Buffer_InsertChar(buffer, 0, 0, 'a');
        Buffer_InsertChar(buffer, 0, 1, 'b');
        Buffer_InsertChar(buffer, 0, 2, 'c');
        Buffer_DeleteChar(buffer, 0, 2); // "abc" -> "ab"

        Line* line = Buffer_GetLine(buffer, 0);
        Slice text = Line_GetText(line, buffer);
        assert(text.size == 2 && memcmp(text.data, "ab", 2) == 0);

        size_t l = SIZE_MAX, c = SIZE_MAX;
        assert(Buffer_Undo(buffer, &l, &c) == true);
        line = Buffer_GetLine(buffer, 0);
        text = Line_GetText(line, buffer);
        assert(text.size == 3 && memcmp(text.data, "abc", 3) == 0);
        assert(l == 0 && c == 3);

        assert(Buffer_Redo(buffer, &l, &c) == true);
        line = Buffer_GetLine(buffer, 0);
        text = Line_GetText(line, buffer);
        assert(text.size == 2 && memcmp(text.data, "ab", 2) == 0);
        assert(l == 0 && c == 2);

        Buffer_Free(buffer);
    }

    // ACTION_SPLIT_LINE
    {
        Buffer* buffer = Buffer_New();
        Buffer_InsertChar(buffer, 0, 0, 'a');
        Buffer_InsertChar(buffer, 0, 1, 'b');
        Buffer_InsertChar(buffer, 0, 2, 'c');
        Buffer_SplitLine(buffer, 0, 1); // "abc" -> "a", "bc"
        assert(Buffer_GetLineCount(buffer) == 2);

        size_t l = SIZE_MAX, c = SIZE_MAX;
        assert(Buffer_Undo(buffer, &l, &c) == true);
        assert(Buffer_GetLineCount(buffer) == 1);
        Line* line = Buffer_GetLine(buffer, 0);
        Slice text = Line_GetText(line, buffer);
        assert(text.size == 3 && memcmp(text.data, "abc", 3) == 0);
        assert(l == 0 && c == 1);

        assert(Buffer_Redo(buffer, &l, &c) == true);
        assert(Buffer_GetLineCount(buffer) == 2);
        Line* redoneLine0 = Buffer_GetLine(buffer, 0);
        Line* redoneLine1 = Buffer_GetLine(buffer, 1);
        Slice l0Text = Line_GetText(redoneLine0, buffer);
        assert(l0Text.size == 1 && memcmp(l0Text.data, "a", 1) == 0);
        Slice l1Text = Line_GetText(redoneLine1, buffer);
        assert(l1Text.size == 2 && memcmp(l1Text.data, "bc", 2) == 0);
        assert(l == 1 && c == 0);

        Buffer_Free(buffer);
    }

    // ACTION_JOIN_LINE
    {
        Buffer* buffer = Buffer_New();
        Buffer_InsertLine(buffer, 1);
        Buffer_InsertChar(buffer, 0, 0, 'a');
        Buffer_InsertChar(buffer, 1, 0, 'b');
        Buffer_JoinLine(buffer, 0); // "a", "b" -> "ab"
        assert(Buffer_GetLineCount(buffer) == 1);

        size_t l = SIZE_MAX, c = SIZE_MAX;
        assert(Buffer_Undo(buffer, &l, &c) == true);
        assert(Buffer_GetLineCount(buffer) == 2);
        Line* line0 = Buffer_GetLine(buffer, 0);
        Line* line1 = Buffer_GetLine(buffer, 1);
        Slice l0Text = Line_GetText(line0, buffer);
        assert(l0Text.size == 1 && memcmp(l0Text.data, "a", 1) == 0);
        Slice l1Text = Line_GetText(line1, buffer);
        assert(l1Text.size == 1 && memcmp(l1Text.data, "b", 1) == 0);
        assert(l == 1 && c == 0);

        assert(Buffer_Redo(buffer, &l, &c) == true);
        assert(Buffer_GetLineCount(buffer) == 1);
        Line* joinedLine = Buffer_GetLine(buffer, 0);
        Slice joinedText = Line_GetText(joinedLine, buffer);
        assert(joinedText.size == 2 && memcmp(joinedText.data, "ab", 2) == 0);
        assert(l == 0 && c == 1);

        Buffer_Free(buffer);
    }
}

TEST(buffer, buffer_undo_then_new_edit_clears_redo)
{
    Buffer* buffer = Buffer_New();
    Buffer_InsertChar(buffer, 0, 0, 'a');

    size_t l = SIZE_MAX, c = SIZE_MAX;
    assert(Buffer_Undo(buffer, &l, &c) == true);

    // A fresh edit after an undo must clear the redo stack.
    Buffer_InsertChar(buffer, 0, 0, 'x');
    assert(Buffer_Redo(buffer, &l, &c) == false);

    Buffer_Free(buffer);
}

// Regression test for commit 8cdea10 (stack node leak when the same
// ActionGroup is undone more than once via undo -> redo -> undo). ASan
// (enabled by default in `make test`) catches any reintroduced leak or
// double-free of the group's snapshotAfter.
TEST(buffer, buffer_redo_stack_reused_group_no_leak)
{
    Buffer* buffer = Buffer_New();
    Buffer_InsertChar(buffer, 0, 0, 'a');

    size_t l = SIZE_MAX, c = SIZE_MAX;
    assert(Buffer_Undo(buffer, &l, &c) == true);
    Line* line = Buffer_GetLine(buffer, 0);
    assert(Line_Length(line) == 0);

    assert(Buffer_Redo(buffer, &l, &c) == true);
    line = Buffer_GetLine(buffer, 0);
    assert(Line_Length(line) == 1);

    assert(Buffer_Undo(buffer, &l, &c) == true);
    line = Buffer_GetLine(buffer, 0);
    assert(Line_Length(line) == 0);

    Buffer_Free(buffer);
}

// Regression test for the perf fix in Buffer_RestoreSnapshot: undo/redo of a
// localized edit deep in a large file must only invalidate the line cache
// from the touched line onward, not wipe and rebuild the entire cache.
TEST(buffer, buffer_undo_redo_scoped_invalidation)
{
    Buffer* buffer = Buffer_New();
    const size_t lineCount = 1000;
    for (size_t i = 1; i < lineCount; i++) {
        Buffer_InsertLine(buffer, i);
    }
    for (size_t i = 0; i < lineCount; i++) {
        char text[16];
        int n = snprintf(text, sizeof(text), "line%zu", i);
        Line* line = Buffer_GetLine(buffer, i);
        Buffer_InsertText(buffer, line->offset, text, (size_t)n);
    }
    assert(Buffer_GetLineCount(buffer) == lineCount);

    // Warm the cache for an unrelated line far from the upcoming edit.
    Line* unrelatedLine = Buffer_GetLine(buffer, 10);
    Slice unrelatedText = Line_GetText(unrelatedLine, buffer);
    char unrelatedData[32];
    memcpy(unrelatedData, unrelatedText.data, unrelatedText.size);
    size_t unrelatedSize = unrelatedText.size;

    // Edit deep in the middle of the file -- a single ACTION_INSERT_CHAR group.
    Buffer_InsertChar(buffer, 500, 0, 'X');
    assert(Buffer_GetLineCount(buffer) == lineCount);

    size_t undoLine = SIZE_MAX, undoColumn = SIZE_MAX;
    assert(Buffer_Undo(buffer, &undoLine, &undoColumn) == true);

    // The dirty range must be tight around line 500, not a full wipe from 0.
    assert(Buffer_PeekDirtyLineStart(buffer) == 500);

    // The unrelated, unaffected line must still be retrievable correctly.
    Line* unrelatedLineAfterUndo = Buffer_GetLine(buffer, 10);
    Slice unrelatedTextAfterUndo = Line_GetText(unrelatedLineAfterUndo, buffer);
    assert(unrelatedTextAfterUndo.size == unrelatedSize);
    assert(memcmp(unrelatedTextAfterUndo.data, unrelatedData, unrelatedSize) == 0);

    Line* editedLine = Buffer_GetLine(buffer, 500);
    Slice editedText = Line_GetText(editedLine, buffer);
    char expected[16];
    int n = snprintf(expected, sizeof(expected), "line%d", 500);
    assert(editedText.size == (size_t)n && memcmp(editedText.data, expected, (size_t)n) == 0);

    // Symmetric check for redo.
    size_t redoLine = SIZE_MAX, redoColumn = SIZE_MAX;
    assert(Buffer_Redo(buffer, &redoLine, &redoColumn) == true);
    assert(Buffer_PeekDirtyLineStart(buffer) == 500);

    Line* unrelatedLineAfterRedo = Buffer_GetLine(buffer, 10);
    Slice unrelatedTextAfterRedo = Line_GetText(unrelatedLineAfterRedo, buffer);
    assert(unrelatedTextAfterRedo.size == unrelatedSize);
    assert(memcmp(unrelatedTextAfterRedo.data, unrelatedData, unrelatedSize) == 0);

    editedLine = Buffer_GetLine(buffer, 500);
    editedText = Line_GetText(editedLine, buffer);
    n = snprintf(expected, sizeof(expected), "Xline%d", 500);
    assert(editedText.size == (size_t)n && memcmp(editedText.data, expected, (size_t)n) == 0);

    Buffer_Free(buffer);
}

TEST(buffer, buffer_undo_redo_empty_stacks)
{
    Buffer* buffer = Buffer_New();

    size_t l = SIZE_MAX, c = SIZE_MAX;
    assert(Buffer_Undo(buffer, &l, &c) == false);
    assert(l == SIZE_MAX && c == SIZE_MAX);

    assert(Buffer_Redo(buffer, &l, &c) == false);
    assert(l == SIZE_MAX && c == SIZE_MAX);

    Buffer_Free(buffer);
}

TEST(buffer, buffer_undo_respects_limit)
{
    Buffer* buffer = Buffer_New();
    buffer->history.undoLimit = 2;

    for (size_t i = 1; i < 5; i++) {
        Buffer_InsertLine(buffer, i);
    }
    for (size_t i = 0; i < 5; i++) {
        // Each edit is on a different line, so every one becomes its own group.
        Buffer_InsertChar(buffer, i, 0, (char)('0' + i));
    }

    assert(buffer->history.undoStack.size == 2);

    size_t l = SIZE_MAX, c = SIZE_MAX;
    assert(Buffer_Undo(buffer, &l, &c) == true);
    assert(Buffer_Undo(buffer, &l, &c) == true);
    assert(Buffer_Undo(buffer, &l, &c) == false); // Earlier groups were evicted by the limit.

    // The evicted groups' edits remain applied -- only their undo history was dropped.
    Line* line0 = Buffer_GetLine(buffer, 0);
    assert(Line_Length(line0) == 1);

    Buffer_Free(buffer);
}

TEST(buffer, buffer_save_fsync_options)
{
    const char* path = "test_temp_fsync_options.txt";
    Slice content = Slice_Make("Line 1\nLine 2\n", 14);
    assert(FileIoWrite(path, content));

    MappedFile mapped = FileIoMmap(path);
    assert(mapped.fileDescriptor != -1);

    Buffer* buffer = Buffer_NewFromMmap(mapped, path);
    assert(buffer != NULL);

    // Modify buffer
    Buffer_InsertText(buffer, 7, "Inserted\n", 9);

    // Save with fsync = true
    Buffer_OnSave(buffer, path, true);
    assert(buffer->isModified == false);

    // Verify written content
    MappedFile mapped2 = FileIoMmap(path);
    assert(mapped2.content.size == 23);
    assert(memcmp(mapped2.content.data, "Line 1\nInserted\nLine 2\n", 23) == 0);
    MappedFile_Unmap(&mapped2);

    // Modify again
    Buffer_InsertText(buffer, 23, "Another", 7);

    // Save with fsync = false
    Buffer_OnSave(buffer, path, false);
    assert(buffer->isModified == false);

    // Verify written content
    MappedFile mapped3 = FileIoMmap(path);
    assert(mapped3.content.size == 30);
    assert(memcmp(mapped3.content.data, "Line 1\nInserted\nLine 2\nAnother", 30) == 0);
    MappedFile_Unmap(&mapped3);

    Buffer_Free(buffer);
    remove(path);
}

