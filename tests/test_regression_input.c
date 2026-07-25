// Regression tests for input-handling behavior (suite regression_input): selection
// lifecycle around destructive commands, undo grouping, paging with wrapped
// lines, and UTF-8 codepoint handling.
//
// Not covered: Editor_MoveLine's NULL checks on its two mallocs. That path is
// only reachable under malloc failure, which needs allocation injection the
// harness does not provide.

#include "../src/core/buffer.h"
#include "../src/core/history.h"
#include "../src/core/line.h"
#include "../src/features/editor.h"
#include "../src/features/input.h"
#include "../src/features/tab.h"
#include "../src/terminal/terminal.h"
#include "../src/utils/clipboard.h"
#include "helpers/test_platform.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// Fixture helpers
// ============================================================================

static Tab* RegressionInputActiveTab(Editor* editor)
{
    return Array_Get(&editor->tabs, Tab*, editor->activeTabIndex);
}

// Sets the text of line `lineIndex`, inserting the line first when it does not
// exist yet (lineIndex must be 0 or one past the current last line).
static void RegressionInputSetLineText(Tab* tab, size_t lineIndex, const char* text)
{
    if (lineIndex >= Buffer_GetLineCount(tab->buffer))
        Buffer_InsertLine(tab->buffer, lineIndex);
    Line* line = Buffer_GetLine(tab->buffer, lineIndex);
    assert(line != NULL);
    Buffer_InsertText(tab->buffer, line->offset, text, strlen(text));
}

static void RegressionInputBuildThreeLineBuffer(Tab* tab)
{
    RegressionInputSetLineText(tab, 0, "one");
    RegressionInputSetLineText(tab, 1, "two");
    RegressionInputSetLineText(tab, 2, "three");
}

static void RegressionInputSelect(Tab* tab, size_t anchorY, size_t anchorX, size_t cursorY, size_t cursorX)
{
    tab->selectStartY = anchorY;
    tab->selectStartX = anchorX;
    tab->cursorY = cursorY;
    tab->cursorX = cursorX;
    tab->hasSelection = true;
}

static void RegressionInputAssertLineEquals(Tab* tab, size_t lineIndex, const char* expected)
{
    Line* line = Buffer_GetLine(tab->buffer, lineIndex);
    assert(line != NULL);
    Slice text = Line_GetText(line, tab->buffer);
    size_t expectedLength = strlen(expected);
    assert(text.size == expectedLength);
    if (expectedLength > 0)
        assert(memcmp(text.data, expected, expectedLength) == 0);
}

// Builds a buffer whose first line wraps into many visual rows: 500 'a's
// followed by three short lines, wrapped at 20 columns.
static void RegressionInputBuildWrappedBuffer(Editor* editor, Tab* tab)
{
    char longLine[500];
    memset(longLine, 'a', sizeof(longLine));
    Line* line0 = Buffer_GetLine(tab->buffer, 0);
    Buffer_InsertText(tab->buffer, line0->offset, longLine, sizeof(longLine));
    RegressionInputSetLineText(tab, 1, "one");
    RegressionInputSetLineText(tab, 2, "two");
    RegressionInputSetLineText(tab, 3, "three");

    editor->screenRows = 10;
    editor->screenColumns = 20;
    Tab_UpdateVisualRows(editor, tab, 20);
    // Fixture sanity: line 0 must wrap into far more visual rows than there
    // are logical lines, so visual-row and line-index quantities diverge.
    assert(Tab_GetVisualRowCount(tab) >= 21);
}

// ============================================================================
// Editor_IndentLines must honor tabSize values above 4: reading tabSize bytes
// from a 4-byte literal "    " would be an out-of-bounds read whenever
// tabSize > 4. A tabSize of 8 indents the line by exactly 8 spaces.
// ============================================================================

TEST(regression_input, indent_with_tab_size_eight_inserts_eight_spaces)
{
    Editor editor;
    Editor_Init(&editor);
    Editor_AddTab(&editor, NULL);
    Tab* tab = RegressionInputActiveTab(&editor);

    editor.config.tabSize = 8; // tab->config points at editor.config

    RegressionInputSetLineText(tab, 0, "abc");
    RegressionInputSelect(tab, 0, 0, 0, 3);

    Editor_ProcessInput(&editor, '\t'); // selection active -> Editor_IndentLines

    RegressionInputAssertLineEquals(tab, 0, "        abc");

    Editor_Free(&editor);
}

// ============================================================================
// Destructive commands must clear (or consume) the selection. A stale
// selection (hasSelection plus the anchor) left pointing at mutated text
// makes the next edit delete text the user never selected.
// ============================================================================

TEST(regression_input, delete_line_clears_selection)
{
    Editor editor;
    Editor_Init(&editor);
    Editor_AddTab(&editor, NULL);
    Tab* tab = RegressionInputActiveTab(&editor);

    RegressionInputBuildThreeLineBuffer(tab);
    RegressionInputSelect(tab, 0, 1, 1, 2);

    Editor_ProcessInput(&editor, editor.config.keyDeleteLine);

    assert(!tab->hasSelection);

    Editor_Free(&editor);
}

TEST(regression_input, kill_to_end_clears_selection)
{
    Editor editor;
    Editor_Init(&editor);
    Editor_AddTab(&editor, NULL);
    Tab* tab = RegressionInputActiveTab(&editor);

    RegressionInputBuildThreeLineBuffer(tab);
    RegressionInputSelect(tab, 0, 1, 1, 1);

    Editor_ProcessInput(&editor, editor.config.keyKillToEnd);

    assert(!tab->hasSelection);

    Editor_Free(&editor);
}

TEST(regression_input, join_lines_clears_selection)
{
    Editor editor;
    Editor_Init(&editor);
    Editor_AddTab(&editor, NULL);
    Tab* tab = RegressionInputActiveTab(&editor);

    RegressionInputBuildThreeLineBuffer(tab);
    RegressionInputSelect(tab, 0, 1, 1, 2);

    Editor_ProcessInput(&editor, editor.config.keyJoinLines);

    assert(!tab->hasSelection);

    Editor_Free(&editor);
}

TEST(regression_input, move_line_up_clears_selection)
{
    Editor editor;
    Editor_Init(&editor);
    Editor_AddTab(&editor, NULL);
    Tab* tab = RegressionInputActiveTab(&editor);

    RegressionInputBuildThreeLineBuffer(tab);
    RegressionInputSelect(tab, 0, 1, 1, 2);

    Editor_ProcessInput(&editor, editor.config.keyMoveLineUp);

    assert(!tab->hasSelection);

    Editor_Free(&editor);
}

TEST(regression_input, move_line_down_clears_selection)
{
    Editor editor;
    Editor_Init(&editor);
    Editor_AddTab(&editor, NULL);
    Tab* tab = RegressionInputActiveTab(&editor);

    RegressionInputBuildThreeLineBuffer(tab);
    RegressionInputSelect(tab, 0, 1, 1, 2);

    Editor_ProcessInput(&editor, editor.config.keyMoveLineDown);

    assert(!tab->hasSelection);

    Editor_Free(&editor);
}

TEST(regression_input, insert_line_below_clears_selection)
{
    Editor editor;
    Editor_Init(&editor);
    Editor_AddTab(&editor, NULL);
    Tab* tab = RegressionInputActiveTab(&editor);

    RegressionInputBuildThreeLineBuffer(tab);
    RegressionInputSelect(tab, 0, 1, 1, 2);

    Editor_ProcessInput(&editor, CTRL_ENTER); // Editor_InsertLineBelow

    assert(!tab->hasSelection);

    Editor_Free(&editor);
}

TEST(regression_input, undo_clears_selection)
{
    Editor editor;
    Editor_Init(&editor);
    Editor_AddTab(&editor, NULL);
    Tab* tab = RegressionInputActiveTab(&editor);

    RegressionInputBuildThreeLineBuffer(tab);
    tab->cursorY = 0;
    tab->cursorX = 0;
    Editor_ProcessInput(&editor, 'x'); // record an undoable edit

    RegressionInputSelect(tab, 0, 1, 1, 2);

    Editor_ProcessInput(&editor, CTRL_KEY('z'));

    assert(!tab->hasSelection);

    Editor_Free(&editor);
}

// ============================================================================
// PAGE_UP/PAGE_DOWN move by visual rows with clamping, and cursorY is always
// a valid line index. Visual-row quantities (rowOffset, rowOffset +
// screenRows - 1) must never be assigned directly to cursorY, a logical line
// index: with wrapping enabled (the default) the two diverge.
// ============================================================================

TEST(regression_input, page_up_keeps_cursor_within_lines)
{
    Editor editor;
    Editor_Init(&editor);
    Editor_AddTab(&editor, NULL);
    Tab* tab = RegressionInputActiveTab(&editor);

    RegressionInputBuildWrappedBuffer(&editor, tab); // 4 logical lines, 28+ visual rows

    // Scrolled 20 VISUAL rows into the wrapped first line: assigning
    // rowOffset straight to cursorY would put cursorY = 20 in a 4-line file.
    tab->rowOffset = 20;
    tab->cursorY = 3;
    tab->cursorX = 0;

    Editor_ProcessInput(&editor, PAGE_UP);

    assert(tab->cursorY < Buffer_GetLineCount(tab->buffer));

    Editor_Free(&editor);
}

TEST(regression_input, page_down_moves_by_visual_rows)
{
    Editor editor;
    Editor_Init(&editor);
    Editor_AddTab(&editor, NULL);
    Tab* tab = RegressionInputActiveTab(&editor);

    RegressionInputBuildWrappedBuffer(&editor, tab); // line 0 alone spans 25 visual rows

    tab->rowOffset = 0;
    tab->cursorY = 0;
    tab->cursorX = 0;

    Editor_ProcessInput(&editor, PAGE_DOWN);

    // One 10-visual-row page from the top lands well inside the 25 visual
    // rows of line 0, so the cursor must stay on logical line 0 rather than
    // jumping to a later logical line.
    assert(tab->cursorY < Buffer_GetLineCount(tab->buffer));
    assert(tab->cursorY == 0);

    Editor_Free(&editor);
}

// ============================================================================
// Plain (unshifted) PAGE_UP/PAGE_DOWN clear the selection, matching the
// behavior of plain arrow keys.
// ============================================================================

TEST(regression_input, page_down_clears_selection)
{
    Editor editor;
    Editor_Init(&editor);
    Editor_AddTab(&editor, NULL);
    Tab* tab = RegressionInputActiveTab(&editor);

    RegressionInputBuildThreeLineBuffer(tab);
    editor.screenRows = 10;
    editor.screenColumns = 40;
    Tab_UpdateVisualRows(&editor, tab, 40);

    RegressionInputSelect(tab, 0, 0, 0, 2);

    Editor_ProcessInput(&editor, PAGE_DOWN);

    assert(!tab->hasSelection);

    Editor_Free(&editor);
}

// ============================================================================
// Editor_DeleteSelection records the deletion as one composite action (e.g.
// via Buffer_DeleteRange), not one Action per deleted byte/join inside the
// group. A single undo must restore the original content regardless of the
// grouping, so that is asserted first.
// ============================================================================

TEST(regression_input, delete_selection_records_single_composite_action)
{
    Editor editor;
    Editor_Init(&editor);
    Editor_AddTab(&editor, NULL);
    Tab* tab = RegressionInputActiveTab(&editor);

    RegressionInputBuildThreeLineBuffer(tab);
    RegressionInputSelect(tab, 0, 0, 2, 5); // select everything

    Editor_DeleteSelection(&editor);

    assert(Buffer_GetLineCount(tab->buffer) == 1);
    RegressionInputAssertLineEquals(tab, 0, "");

    // One undo restores the whole selection delete.
    size_t outLine = 0;
    size_t outColumn = 0;
    assert(Buffer_Undo(tab->buffer, &outLine, &outColumn));
    assert(Buffer_GetLineCount(tab->buffer) == 3);
    RegressionInputAssertLineEquals(tab, 0, "one");
    RegressionInputAssertLineEquals(tab, 1, "two");
    RegressionInputAssertLineEquals(tab, 2, "three");

    // Redo pushes the group back on top of the undo stack; inspect it there.
    assert(Buffer_Redo(tab->buffer, &outLine, &outColumn));
    assert(Buffer_GetLineCount(tab->buffer) == 1);

    Stack* undoStack = &tab->buffer->history.undoStack;
    assert(Array_Size(undoStack) >= 1);
    ActionGroup* topGroup = Array_Get(undoStack, ActionGroup*, Array_Size(undoStack) - 1);
    // Exactly one composite action -- byte-by-byte recording would produce
    // one action per deleted byte plus line joins (14 for this selection).
    assert(Array_Size(&topGroup->actions) == 1);

    Editor_Free(&editor);
}

// ============================================================================
// A zero-width selection (anchor == cursor) behaves as no selection. Treating
// it as a real selection would make cut clobber the clipboard with empty
// content, and route Backspace/typing into Editor_DeleteSelection, which
// deletes nothing (while pushing an empty undo group and wiping the redo
// stack).
// ============================================================================

TEST(regression_input, zero_width_cut_preserves_clipboard)
{
    TestPlatform testPlatform;
    TestPlatform_Init(&testPlatform);
    Editor editor;
    Editor_Init(&editor);
    TestPlatform_Attach(&testPlatform, &editor);
    Editor_AddTab(&editor, NULL);
    Tab* tab = RegressionInputActiveTab(&editor);

    RegressionInputSetLineText(tab, 0, "ab");
    Clipboard_Write("keep");

    RegressionInputSelect(tab, 0, 1, 0, 1); // zero-width selection

    Editor_ProcessInput(&editor, editor.config.keyCut);

    Slice clipboardText = Clipboard_Read();
    assert(clipboardText.size == 4);
    assert(memcmp(clipboardText.data, "keep", 4) == 0);

    Clipboard_Free();
    Editor_Free(&editor);
    TestPlatform_Free(&testPlatform);
}

TEST(regression_input, zero_width_backspace_deletes_one_char)
{
    Editor editor;
    Editor_Init(&editor);
    Editor_AddTab(&editor, NULL);
    Tab* tab = RegressionInputActiveTab(&editor);

    RegressionInputSetLineText(tab, 0, "ab");
    RegressionInputSelect(tab, 0, 2, 0, 2); // zero-width selection at end of line

    Editor_ProcessInput(&editor, BACKSPACE);

    // A zero-width selection counts as no selection, so Backspace deletes the
    // character before the cursor instead of routing into
    // Editor_DeleteSelection and deleting nothing.
    RegressionInputAssertLineEquals(tab, 0, "a");

    Editor_Free(&editor);
}

// ============================================================================
// Type-over-selection and paste-over-selection record ONE undo group, not two
// (the selection delete, then the insert): a single undo restores the
// original text without exposing an intermediate state.
// ============================================================================

TEST(regression_input, typeover_restores_original_with_single_undo)
{
    Editor editor;
    Editor_Init(&editor);
    Editor_AddTab(&editor, NULL);
    Tab* tab = RegressionInputActiveTab(&editor);

    RegressionInputSetLineText(tab, 0, "hello");
    RegressionInputSelect(tab, 0, 1, 0, 4); // selects "ell"

    Editor_ProcessInput(&editor, 'X');
    RegressionInputAssertLineEquals(tab, 0, "hXo");

    size_t outLine = 0;
    size_t outColumn = 0;
    assert(Buffer_Undo(tab->buffer, &outLine, &outColumn));
    // One undo restores "hello". If delete and insert were recorded as
    // separate groups, it would expose the intermediate "ho" state instead.
    RegressionInputAssertLineEquals(tab, 0, "hello");

    Editor_Free(&editor);
}

TEST(regression_input, paste_over_selection_restores_original_with_single_undo)
{
    TestPlatform testPlatform;
    TestPlatform_Init(&testPlatform);
    Editor editor;
    Editor_Init(&editor);
    TestPlatform_Attach(&testPlatform, &editor);
    Editor_AddTab(&editor, NULL);
    Tab* tab = RegressionInputActiveTab(&editor);

    RegressionInputSetLineText(tab, 0, "hello");
    Clipboard_Write("12");
    RegressionInputSelect(tab, 0, 1, 0, 4); // selects "ell"

    Editor_ProcessInput(&editor, editor.config.keyPaste);
    RegressionInputAssertLineEquals(tab, 0, "h12o");

    size_t outLine = 0;
    size_t outColumn = 0;
    assert(Buffer_Undo(tab->buffer, &outLine, &outColumn));
    // One undo restores "hello", never the intermediate "ho".
    RegressionInputAssertLineEquals(tab, 0, "hello");

    Clipboard_Free();
    Editor_Free(&editor);
    TestPlatform_Free(&testPlatform);
}

// ============================================================================
// The quit and close-tab confirmations must not share one static isQuiting
// flag in Editor_ProcessKeypress: confirming one would arm the other, letting
// a quit right after a close-tab warning exit without its own warning.
//
// Shaped as TEST_EXITS(7) because the failure mode is a clean exit(0) inside
// the second keypress, which no in-test assert could observe: the test passes
// only if both presses merely warn and control reaches the trailing exit(7).
// ============================================================================

TEST_EXITS(regression_input, close_tab_confirm_does_not_satisfy_quit, 7)
{
    TestPlatform testPlatform;
    TestPlatform_Init(&testPlatform);
    Editor editor;
    Editor_Init(&editor);
    TestPlatform_Attach(&testPlatform, &editor);
    Editor_AddTab(&editor, NULL);

    Editor_ProcessInput(&editor, 'x'); // make the tab unsaved

    int keys[2];
    keys[0] = editor.config.keyCloseTab;
    keys[1] = editor.config.keyQuit;
    TestPlatform_SetKeys(&testPlatform, keys, 2);

    Editor_ProcessKeypress(&editor); // close-tab: warns about unsaved changes
    Editor_ProcessKeypress(&editor); // quit: must warn on its own, not reuse the close-tab confirmation

    Editor_Free(&editor);
    TestPlatform_Free(&testPlatform);
    exit(7);
}

// ============================================================================
// UTF-8 codepoint handling: arrows/Backspace/Delete move over and delete
// whole codepoints, never single bytes inside a multi-byte sequence, and
// typed bytes >= 0x80 are inserted rather than dropped. "\xC3\xA9" is U+00E9
// (e-acute).
// ============================================================================

TEST(regression_input, arrow_right_skips_multibyte_codepoint)
{
    Editor editor;
    Editor_Init(&editor);
    Editor_AddTab(&editor, NULL);
    Tab* tab = RegressionInputActiveTab(&editor);

    RegressionInputSetLineText(tab, 0, "a\xC3\xA9" "b");
    tab->cursorY = 0;
    tab->cursorX = 1; // just before the two-byte codepoint

    Editor_MoveCursor(&editor, ARROW_RIGHT);

    assert(tab->cursorX == 3); // past the whole codepoint, not inside it

    Editor_Free(&editor);
}

TEST(regression_input, arrow_left_skips_multibyte_codepoint)
{
    Editor editor;
    Editor_Init(&editor);
    Editor_AddTab(&editor, NULL);
    Tab* tab = RegressionInputActiveTab(&editor);

    RegressionInputSetLineText(tab, 0, "a\xC3\xA9" "b");
    tab->cursorY = 0;
    tab->cursorX = 3; // just after the two-byte codepoint

    Editor_MoveCursor(&editor, ARROW_LEFT);

    assert(tab->cursorX == 1); // before the whole codepoint, not inside it

    Editor_Free(&editor);
}

TEST(regression_input, backspace_deletes_whole_codepoint)
{
    Editor editor;
    Editor_Init(&editor);
    Editor_AddTab(&editor, NULL);
    Tab* tab = RegressionInputActiveTab(&editor);

    RegressionInputSetLineText(tab, 0, "a\xC3\xA9");
    tab->cursorY = 0;
    tab->cursorX = 3; // end of line, after the codepoint

    Editor_ProcessInput(&editor, BACKSPACE);

    // The whole codepoint goes; removing only the trailing byte would leave
    // a dangling 0xC3 lead byte.
    RegressionInputAssertLineEquals(tab, 0, "a");

    Editor_Free(&editor);
}

TEST(regression_input, delete_key_removes_whole_codepoint)
{
    Editor editor;
    Editor_Init(&editor);
    Editor_AddTab(&editor, NULL);
    Tab* tab = RegressionInputActiveTab(&editor);

    RegressionInputSetLineText(tab, 0, "a\xC3\xA9" "b");
    tab->cursorY = 0;
    tab->cursorX = 1; // cursor on the codepoint

    Editor_ProcessInput(&editor, DELETE_KEY);

    // Both bytes of the codepoint go; removing only 0xC3 would leave a
    // dangling 0xA9 continuation byte.
    RegressionInputAssertLineEquals(tab, 0, "ab");

    Editor_Free(&editor);
}

TEST(regression_input, typing_high_bytes_inserts_sequence)
{
    Editor editor;
    Editor_Init(&editor);
    Editor_AddTab(&editor, NULL);
    Tab* tab = RegressionInputActiveTab(&editor);

    Editor_ProcessInput(&editor, 0xC3);
    Editor_ProcessInput(&editor, 0xA9);

    // The UTF-8 bytes are inserted; an `input < 128` printable filter would
    // silently drop them and leave the line empty.
    RegressionInputAssertLineEquals(tab, 0, "\xC3\xA9");

    Editor_Free(&editor);
}
