#include "../src/features/editor.h"
#include "../src/features/tab.h"
#include "../src/features/input.h"
#include "../src/core/buffer.h"
#include "../src/core/line.h"
#include "../src/utils/clipboard.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static void test_move_cursor_left_right_within_line(void)
{
    Editor editor;
    Editor_Init(&editor);
    Editor_AddTab(&editor, NULL);
    Tab* tab = Array_Get(&editor.tabs, Tab*, editor.activeTabIndex);

    Line* line = Buffer_GetLine(tab->buffer, 0);
    Buffer_InsertText(tab->buffer, line->offset, "hello", 5);
    tab->cursorX = 0;
    tab->cursorY = 0;

    for (size_t i = 0; i < 5; i++) {
        Editor_MoveCursor(&editor, ARROW_RIGHT);
    }
    assert(tab->cursorX == 5);

    Editor_MoveCursor(&editor, ARROW_RIGHT); // No-op past the end of a single-line buffer.
    assert(tab->cursorX == 5);

    for (size_t i = 0; i < 5; i++) {
        Editor_MoveCursor(&editor, ARROW_LEFT);
    }
    assert(tab->cursorX == 0);

    Editor_MoveCursor(&editor, ARROW_LEFT); // No-op at the start of a single-line buffer.
    assert(tab->cursorX == 0);

    Editor_Free(&editor);
}

static void test_move_cursor_left_right_across_lines(void)
{
    Editor editor;
    Editor_Init(&editor);
    Editor_AddTab(&editor, NULL);
    Tab* tab = Array_Get(&editor.tabs, Tab*, editor.activeTabIndex);

    Line* line0 = Buffer_GetLine(tab->buffer, 0);
    Buffer_InsertText(tab->buffer, line0->offset, "hello", 5);
    Buffer_InsertLine(tab->buffer, 1);
    Line* line1 = Buffer_GetLine(tab->buffer, 1);
    Buffer_InsertText(tab->buffer, line1->offset, "hi", 2);

    tab->cursorY = 1;
    tab->cursorX = 0;
    Editor_MoveCursor(&editor, ARROW_LEFT);
    assert(tab->cursorY == 0);
    assert(tab->cursorX == 5); // End of line 0.

    Editor_MoveCursor(&editor, ARROW_RIGHT);
    assert(tab->cursorY == 1);
    assert(tab->cursorX == 0); // Start of line 1.

    Editor_Free(&editor);
}

static void test_move_cursor_up_down_clamps_column(void)
{
    Editor editor;
    Editor_Init(&editor);
    Editor_AddTab(&editor, NULL);
    Tab* tab = Array_Get(&editor.tabs, Tab*, editor.activeTabIndex);

    Line* line0 = Buffer_GetLine(tab->buffer, 0);
    Buffer_InsertText(tab->buffer, line0->offset, "hello", 5);
    Buffer_InsertLine(tab->buffer, 1);
    Line* line1 = Buffer_GetLine(tab->buffer, 1);
    Buffer_InsertText(tab->buffer, line1->offset, "hi", 2);

    Tab_UpdateVisualRows((const Editor*)NULL, tab, 80);

    tab->cursorY = 0;
    tab->cursorX = 5; // End of "hello".
    Editor_MoveCursor(&editor, ARROW_DOWN);
    assert(tab->cursorY == 1);
    assert(tab->cursorX == 2); // Clamped to the length of "hi", not the original column.

    Editor_MoveCursor(&editor, ARROW_UP);
    assert(tab->cursorY == 0);
    assert(tab->cursorX == 2); // Column 2 fits on line 0, so it's preserved (not clamped).

    Editor_Free(&editor);
}

static void test_move_cursor_word_right_left(void)
{
    Editor editor;
    Editor_Init(&editor);
    Editor_AddTab(&editor, NULL);
    Tab* tab = Array_Get(&editor.tabs, Tab*, editor.activeTabIndex);

    const char* text = "  foo   bar";
    Line* line0 = Buffer_GetLine(tab->buffer, 0);
    Buffer_InsertText(tab->buffer, line0->offset, text, strlen(text));

    tab->cursorY = 0;
    tab->cursorX = 0;

    Editor_MoveCursorWord(&editor, CTRL_ARROW_RIGHT);
    assert(tab->cursorX == 2); // Start of "foo".

    Editor_MoveCursorWord(&editor, CTRL_ARROW_RIGHT);
    assert(tab->cursorX == 8); // Start of "bar".

    Editor_MoveCursorWord(&editor, CTRL_ARROW_RIGHT);
    assert(tab->cursorX == 11); // End of the line.

    Editor_MoveCursorWord(&editor, CTRL_ARROW_LEFT);
    assert(tab->cursorX == 8); // Back to start of "bar".

    Editor_MoveCursorWord(&editor, CTRL_ARROW_LEFT);
    assert(tab->cursorX == 2); // Back to start of "foo".

    Editor_MoveCursorWord(&editor, CTRL_ARROW_LEFT);
    assert(tab->cursorX == 0); // Back to the start of the line.

    // Cross-line case: CTRL_ARROW_LEFT at column 0 jumps to the end of the previous line.
    Buffer_InsertLine(tab->buffer, 1);
    tab->cursorY = 1;
    tab->cursorX = 0;
    Editor_MoveCursorWord(&editor, CTRL_ARROW_LEFT);
    assert(tab->cursorY == 0);
    assert(tab->cursorX == 11);

    Editor_Free(&editor);
}

static void test_get_selection_normalizes_direction(void)
{
    Tab tab;
    Tab_Init(&tab);

    // Forward drag: selection start before the cursor, same line.
    tab.selectStartY = 0;
    tab.selectStartX = 2;
    tab.cursorY = 0;
    tab.cursorX = 5;

    size_t startX, startY, endX, endY;
    Tab_GetSelection(&tab, &startX, &startY, &endX, &endY);
    assert(startY == 0 && startX == 2);
    assert(endY == 0 && endX == 5);

    // Backward drag: same endpoints, opposite start/cursor roles -- must normalize identically.
    tab.selectStartY = 0;
    tab.selectStartX = 5;
    tab.cursorY = 0;
    tab.cursorX = 2;

    Tab_GetSelection(&tab, &startX, &startY, &endX, &endY);
    assert(startY == 0 && startX == 2);
    assert(endY == 0 && endX == 5);

    // Multi-line: ordering is by line first, regardless of column values.
    tab.selectStartY = 0;
    tab.selectStartX = 3;
    tab.cursorY = 1;
    tab.cursorX = 1;

    Tab_GetSelection(&tab, &startX, &startY, &endX, &endY);
    assert(startY == 0 && startX == 3);
    assert(endY == 1 && endX == 1);

    Tab_Free(&tab);
}

static void test_delete_selection_single_and_multi_line(void)
{
    // Single-line selection.
    {
        Editor editor;
        Editor_Init(&editor);
        Editor_AddTab(&editor, NULL);
        Tab* tab = Array_Get(&editor.tabs, Tab*, editor.activeTabIndex);

        Line* line0 = Buffer_GetLine(tab->buffer, 0);
        Buffer_InsertText(tab->buffer, line0->offset, "hello world", 11);

        tab->selectStartY = 0;
        tab->selectStartX = 0;
        tab->cursorY = 0;
        tab->cursorX = 5; // Selects "hello".
        tab->hasSelection = true;

        Editor_DeleteSelection(&editor);

        Line* line = Buffer_GetLine(tab->buffer, 0);
        Slice text = Line_GetText(line, tab->buffer);
        assert(text.size == 6 && memcmp(text.data, " world", 6) == 0);
        assert(tab->hasSelection == false);

        Editor_Free(&editor);
    }

    // Multi-line selection spanning a join.
    {
        Editor editor;
        Editor_Init(&editor);
        Editor_AddTab(&editor, NULL);
        Tab* tab = Array_Get(&editor.tabs, Tab*, editor.activeTabIndex);

        Line* line0 = Buffer_GetLine(tab->buffer, 0);
        Buffer_InsertText(tab->buffer, line0->offset, "foo", 3);
        Buffer_InsertLine(tab->buffer, 1);
        Line* line1 = Buffer_GetLine(tab->buffer, 1);
        Buffer_InsertText(tab->buffer, line1->offset, "bar", 3);

        tab->selectStartY = 0;
        tab->selectStartX = 1; // Selects "o" (line 0) through "b" (line 1).
        tab->cursorY = 1;
        tab->cursorX = 1;
        tab->hasSelection = true;

        Editor_DeleteSelection(&editor);

        assert(Buffer_GetLineCount(tab->buffer) == 1);
        Line* line = Buffer_GetLine(tab->buffer, 0);
        Slice text = Line_GetText(line, tab->buffer);
        assert(text.size == 3 && memcmp(text.data, "far", 3) == 0);
        assert(tab->hasSelection == false);

        Editor_Free(&editor);
    }

    // Read-only buffer: deleting a selection is a no-op.
    {
        Editor editor;
        Editor_Init(&editor);
        Editor_AddTab(&editor, NULL);
        Tab* tab = Array_Get(&editor.tabs, Tab*, editor.activeTabIndex);

        Line* line0 = Buffer_GetLine(tab->buffer, 0);
        Buffer_InsertText(tab->buffer, line0->offset, "hello", 5);
        tab->buffer->isReadOnly = true;

        tab->selectStartY = 0;
        tab->selectStartX = 0;
        tab->cursorY = 0;
        tab->cursorX = 5;
        tab->hasSelection = true;

        Editor_DeleteSelection(&editor);

        Line* line = Buffer_GetLine(tab->buffer, 0);
        Slice text = Line_GetText(line, tab->buffer);
        assert(text.size == 5 && memcmp(text.data, "hello", 5) == 0);
        assert(tab->hasSelection == true);

        Editor_Free(&editor);
    }
}

static void test_toggle_fold_on_foldable_and_non_foldable_line(void)
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
    Buffer_InsertText(tab->buffer, line2->offset, "}", 1);

    assert(Line_IsFoldable(tab->buffer, 0, tab->config->tabSize) == true);
    assert(Line_IsFoldable(tab->buffer, 1, tab->config->tabSize) == false);

    // Foldable line: toggling flips isFolded and adjusts foldedLineCount.
    tab->cursorY = 0;
    Editor_ToggleFold(&editor);
    line0 = Buffer_GetLine(tab->buffer, 0);
    assert(line0->isFolded == true);
    assert(tab->buffer->foldedLineCount == 1);

    Editor_ToggleFold(&editor);
    line0 = Buffer_GetLine(tab->buffer, 0);
    assert(line0->isFolded == false);
    assert(tab->buffer->foldedLineCount == 0);

    // Non-foldable line: toggling is a no-op.
    tab->cursorY = 1;
    Editor_ToggleFold(&editor);
    line1 = Buffer_GetLine(tab->buffer, 1);
    assert(line1->isFolded == false);
    assert(tab->buffer->foldedLineCount == 0);

    Editor_Free(&editor);
}

static void test_auto_unfold_on_edit_or_movement(void)
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
    Buffer_InsertText(tab->buffer, line2->offset, "}", 1);

    tab->cursorY = 0;
    Editor_ToggleFold(&editor);
    line0 = Buffer_GetLine(tab->buffer, 0);
    assert(line0->isFolded == true);
    assert(tab->buffer->foldedLineCount == 1);

    Buffer_EnsureLineVisible(tab->buffer, 1, tab->config->tabSize);
    line0 = Buffer_GetLine(tab->buffer, 0);
    assert(line0->isFolded == false);
    assert(tab->buffer->foldedLineCount == 0);

    Editor_Free(&editor);
}

static void test_visual_rows_cache_updates_on_unfold(void)
{
    Editor editor;
    Editor_Init(&editor);
    Editor_AddTab(&editor, NULL);
    Tab* tab = Array_Get(&editor.tabs, Tab*, editor.activeTabIndex);
    tab->config->wrapLines = false;

    Line* line0 = Buffer_GetLine(tab->buffer, 0);
    Buffer_InsertText(tab->buffer, line0->offset, "if (cond) {", 11);

    Buffer_InsertLine(tab->buffer, 1);
    Line* line1 = Buffer_GetLine(tab->buffer, 1);
    Buffer_InsertText(tab->buffer, line1->offset, "    foo();", 10);

    Buffer_InsertLine(tab->buffer, 2);
    Line* line2 = Buffer_GetLine(tab->buffer, 2);
    Buffer_InsertText(tab->buffer, line2->offset, "}", 1);

    Tab_UpdateVisualRows(&editor, tab, 80);
    assert(tab->visualRowsFoldedCount == 0);

    tab->cursorY = 0;
    Editor_ToggleFold(&editor);
    assert(tab->buffer->foldedLineCount == 1);

    Tab_UpdateVisualRows(&editor, tab, 80);
    assert(tab->visualRowsFoldedCount == 1);

    Editor_ToggleFold(&editor);
    assert(tab->buffer->foldedLineCount == 0);

    Tab_UpdateVisualRows(&editor, tab, 80);
    assert(tab->visualRowsFoldedCount == 0);

    Editor_Free(&editor);
}

static void test_input_tab_insertion(void)
{
    Editor editor;
    Editor_Init(&editor);
    Editor_AddTab(&editor, NULL);
    Tab* tab = Array_Get(&editor.tabs, Tab*, editor.activeTabIndex);

    Editor_ProcessInput(&editor, '\t');

    Line* line = Buffer_GetLine(tab->buffer, 0);
    Slice text = Line_GetText(line, tab->buffer);
    assert(text.size == 1 && ((const char*)text.data)[0] == '\t');
    assert(tab->cursorX == 1);

    Editor_Free(&editor);
}

static void test_clipboard_basic(void)
{
    Clipboard_Write("hello clipboard world");
    Slice s = Clipboard_Read();
    assert(s.size == 21);
    assert(strncmp((const char*)s.data, "hello clipboard world", 21) == 0);
    assert(((const char*)s.data)[21] == '\0');

    Clipboard_Write("short");
    s = Clipboard_Read();
    assert(s.size == 5);
    assert(strncmp((const char*)s.data, "short", 5) == 0);
    assert(((const char*)s.data)[5] == '\0');

    Clipboard_Write("a much longer string to force expansion of the internal array capacity");
    s = Clipboard_Read();
    assert(s.size == 70);
    assert(strncmp((const char*)s.data, "a much longer string to force expansion of the internal array capacity", 70) == 0);
    assert(((const char*)s.data)[70] == '\0');

    Clipboard_Free();
}

static void test_tab_get_selected_text(void)
{
    Editor editor;
    Editor_Init(&editor);
    Editor_AddTab(&editor, NULL);
    Tab* tab = Array_Get(&editor.tabs, Tab*, editor.activeTabIndex);

    Line* line0 = Buffer_GetLine(tab->buffer, 0);
    Buffer_InsertText(tab->buffer, line0->offset, "hello", 5);
    Buffer_InsertLine(tab->buffer, 1);
    Line* line1 = Buffer_GetLine(tab->buffer, 1);
    Buffer_InsertText(tab->buffer, line1->offset, "world", 5);

    tab->hasSelection = false;
    char* text = Tab_GetSelectedText(tab);
    assert(text == NULL);

    tab->hasSelection = true;
    tab->selectStartY = 0;
    tab->selectStartX = 1;
    tab->cursorY = 0;
    tab->cursorX = 4;
    text = Tab_GetSelectedText(tab);
    assert(text != NULL);
    assert(strcmp(text, "ell") == 0);
    free(text);

    tab->selectStartY = 0;
    tab->selectStartX = 3;
    tab->cursorY = 1;
    tab->cursorX = 2;
    text = Tab_GetSelectedText(tab);
    assert(text != NULL);
    assert(strcmp(text, "lo\nwo") == 0);
    free(text);

    Editor_Free(&editor);
}

static void test_tab_copy_selection(void)
{
    Editor editor;
    Editor_Init(&editor);
    Editor_AddTab(&editor, NULL);
    Tab* tab = Array_Get(&editor.tabs, Tab*, editor.activeTabIndex);

    Line* line0 = Buffer_GetLine(tab->buffer, 0);
    Buffer_InsertText(tab->buffer, line0->offset, "hello", 5);
    Buffer_InsertLine(tab->buffer, 1);
    Line* line1 = Buffer_GetLine(tab->buffer, 1);
    Buffer_InsertText(tab->buffer, line1->offset, "world", 5);

    tab->hasSelection = true;
    tab->selectStartY = 0;
    tab->selectStartX = 3;
    tab->cursorY = 1;
    tab->cursorX = 2;

    Tab_CopySelection(tab);

    Slice s = Clipboard_Read();
    assert(s.size == 5);
    assert(strncmp((const char*)s.data, "lo\nwo", 5) == 0);

    Clipboard_Free();
    Editor_Free(&editor);
}

static void test_compound_edit_undo_redo(void)
{
    Editor editor;
    Editor_Init(&editor);
    Editor_AddTab(&editor, NULL);
    Tab* tab = Array_Get(&editor.tabs, Tab*, editor.activeTabIndex);

    Line* line = Buffer_GetLine(tab->buffer, 0);
    Buffer_InsertText(tab->buffer, line->offset, "one two", 7);
    tab->cursorX = 7;

    Editor_DeleteWord(&editor, -1);
    line = Buffer_GetLine(tab->buffer, 0);
    Slice text = Line_GetText(line, tab->buffer);
    assert(text.size == 4 && memcmp(text.data, "one ", 4) == 0);

    size_t lineNumber = SIZE_MAX;
    size_t column = SIZE_MAX;
    assert(Buffer_Undo(tab->buffer, &lineNumber, &column) == true);
    line = Buffer_GetLine(tab->buffer, 0);
    text = Line_GetText(line, tab->buffer);
    assert(text.size == 7 && memcmp(text.data, "one two", 7) == 0);

    assert(Buffer_Redo(tab->buffer, &lineNumber, &column) == true);
    line = Buffer_GetLine(tab->buffer, 0);
    text = Line_GetText(line, tab->buffer);
    assert(text.size == 4 && memcmp(text.data, "one ", 4) == 0);

    Editor_Free(&editor);

    Editor_Init(&editor);
    Editor_AddTab(&editor, NULL);
    tab = Array_Get(&editor.tabs, Tab*, editor.activeTabIndex);
    Editor_ProcessInput(&editor, CTRL_KEY('z'));
    assert(tab->isSaved == true);
    Editor_Free(&editor);
}

static void test_paste_undo_grouping(void)
{
    Editor editor;
    Editor_Init(&editor);
    Editor_AddTab(&editor, NULL);
    Tab* tab = Array_Get(&editor.tabs, Tab*, editor.activeTabIndex);

    Line* line0 = Buffer_GetLine(tab->buffer, 0);
    Buffer_InsertText(tab->buffer, line0->offset, "start", 5);
    tab->cursorY = 0;
    tab->cursorX = 5;

    Clipboard_Write("line1\nline2\nline3");

    Editor_ProcessInput(&editor, editor.config.keyPaste);

    assert(Buffer_GetLineCount(tab->buffer) == 3);
    Line* l2 = Buffer_GetLine(tab->buffer, 2);
    Slice s2 = Line_GetText(l2, tab->buffer);
    assert(strncmp((const char*)s2.data, "line3", 5) == 0);

    size_t outLine, outCol;
    Buffer_Undo(tab->buffer, &outLine, &outCol);
    assert(Buffer_GetLineCount(tab->buffer) == 1);
    Line* l0 = Buffer_GetLine(tab->buffer, 0);
    Slice s0 = Line_GetText(l0, tab->buffer);
    assert(s0.size == 5);
    assert(strncmp((const char*)s0.data, "start", 5) == 0);

    Buffer_Redo(tab->buffer, &outLine, &outCol);
    assert(Buffer_GetLineCount(tab->buffer) == 3);
    l2 = Buffer_GetLine(tab->buffer, 2);
    s2 = Line_GetText(l2, tab->buffer);
    assert(strncmp((const char*)s2.data, "line3", 5) == 0);

    tab->hasSelection = true;
    tab->selectStartY = 0;
    tab->selectStartX = 5;
    tab->cursorY = 2;
    tab->cursorX = 5;
    
    Editor_ProcessInput(&editor, '\x7f');
    
    assert(Buffer_GetLineCount(tab->buffer) == 1);
    l0 = Buffer_GetLine(tab->buffer, 0);
    s0 = Line_GetText(l0, tab->buffer);
    assert(s0.size == 5);
    assert(strncmp((const char*)s0.data, "start", 5) == 0);

    Buffer_Undo(tab->buffer, &outLine, &outCol);
    assert(Buffer_GetLineCount(tab->buffer) == 3);

    Clipboard_Free();
    Editor_Free(&editor);
}
