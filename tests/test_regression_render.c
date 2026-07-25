// Regression tests for rendering / editor-shell edge cases: stale incremental
// visual-row splices, unbounded copies into fixed buffers, degenerate
// terminal geometry, byte-oriented UTF-8 truncation, and off-by-one
// selection indices.
//
// Single-TU: #included into tests/main.c after the other suites.

#include "../src/core/buffer.h"
#include "../src/core/line.h"
#include "../src/features/editor.h"
#include "../src/features/explorer.h"
#include "../src/features/input.h"
#include "../src/features/logs_view.h"
#include "../src/features/tab.h"
#include "../src/utils/array.h"
#include "../src/utils/logger.h"
#include "helpers/test_platform.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Shared helpers (names kept distinctive: all test files share one TU).
// ---------------------------------------------------------------------------

// Seeds a fresh standalone Tab's buffer with three lines, one of which wraps
// at width 10:
//   line 0: "aaaa"              (4 bytes, 1 visual row)
//   line 1: "0123456789ABCDEF"  (16 bytes, wraps into rows of 10 + 6)
//   line 2: "zzzz"              (4 bytes, 1 visual row)
static void RegressionRenderSeedWrappingBuffer(Tab* tab)
{
    static const char seedText[] = "aaaa\n0123456789ABCDEF\nzzzz";
    Buffer_InsertText(tab->buffer, 0, seedText, sizeof(seedText) - 1);
    assert(Buffer_GetLineCount(tab->buffer) == 3);
}

// Applies three raw buffer edits back-to-back WITHOUT refreshing visual rows
// in between. Each edit internally triggers a lazy line-cache rebuild for the
// PREVIOUS edit's dirty range (Buffer_GetLine / Buffer_GetLineCount rebuild on
// access), so by the time visual rows are updated once at the end, only the
// LAST edit's range is still recorded in buffer->lastRebuilt*.
//   edit 1: line 0 grows from 4 to 16 bytes (now wraps into 2 rows)
//   edit 2: line 1 splits at column 8 ("01234567" / "89ABCDEF")
//   edit 3: 'Q' prepended to the last line ("zzzz" -> "Qzzzz", now line 3)
static void RegressionRenderApplyUnsyncedEditBurst(Tab* tab)
{
    Line* lineZero = Buffer_GetLine(tab->buffer, 0);
    Buffer_InsertText(tab->buffer, lineZero->offset + lineZero->length, "XXXXXXXXXXXX", 12);
    Buffer_SplitLine(tab->buffer, 1, 8);
    Buffer_InsertChar(tab->buffer, 3, 0, 'Q');
    assert(Buffer_GetLineCount(tab->buffer) == 4);
}

// Element-by-element comparison of two VisualRow arrays.
static void RegressionRenderAssertVisualRowsIdentical(const Array* actualRows, const Array* expectedRows)
{
    assert(Array_Size(actualRows) == Array_Size(expectedRows));
    for (size_t index = 0; index < Array_Size(expectedRows); index++) {
        VisualRow* actual = (VisualRow*)Array_At(actualRows, index);
        VisualRow* expected = (VisualRow*)Array_At(expectedRows, index);
        assert(actual->lineIndex == expected->lineIndex);
        assert(actual->startCol == expected->startCol);
        assert(actual->length == expected->length);
        assert(actual->isWrapped == expected->isWrapped);
    }
}

// Scans every byte written to the fake terminal and rejects any 0xC3 UTF-8
// lead byte that is not immediately followed by a continuation byte
// (0x80-0xBF). The tests below only emit multi-byte text as U+00E9 "e-acute"
// (0xC3 0xA9); everything else in a frame (escape sequences, ASCII text,
// padding spaces) never contains 0xC3, so an orphaned 0xC3 proves a UTF-8
// sequence was split by byte-oriented truncation.
static bool RegressionRenderFrameKeepsTwoByteUtf8Intact(const TestPlatform* testPlatform)
{
    const unsigned char* bytes = (const unsigned char*)testPlatform->screen.data;
    size_t size = Array_Size((Array*)&testPlatform->screen);
    for (size_t index = 0; index < size; index++) {
        if (bytes[index] == 0xC3) {
            if (index + 1 >= size || (bytes[index + 1] & 0xC0) != 0x80) {
                return false;
            }
        }
    }
    return true;
}

// Fills `destination` with a leading ASCII byte followed by `pairCount`
// repetitions of U+00E9 (0xC3 0xA9), NUL-terminated. The odd leading byte
// guarantees any even-byte-count truncation lands mid-codepoint.
static void RegressionRenderBuildEacuteRun(char* destination, size_t destinationSize, char leadingByte, size_t pairCount)
{
    assert(destinationSize >= 1 + pairCount * 2 + 1);
    size_t position = 0;
    destination[position++] = leadingByte;
    for (size_t pair = 0; pair < pairCount; pair++) {
        destination[position++] = (char)0xC3;
        destination[position++] = (char)0xA9;
    }
    destination[position] = '\0';
}

// ---------------------------------------------------------------------------
// Tab_TryUpdateVisualRowsIncremental splices wrap segments based on the range
// of the most recent line-cache rebuild (buffer->lastRebuilt*). When several
// edits happen between two renders, each edit's lazy rebuild overwrites that
// range, so a splice that consults only the final range would leave all but
// the last edit's wrap segments unrespliced and the cached VisualRows stale
// (wrong lengths, unshifted line indices). A multi-edit frame must produce
// visual rows identical to a full from-scratch rebuild of the same final
// content.
// ---------------------------------------------------------------------------
TEST(regression_render, visual_rows_multiple_edits_match_full_rebuild)
{
    Tab editedTab;
    Tab_Init(&editedTab);
    assert(editedTab.config->wrapLines == true);

    const size_t usableColumns = 10;

    RegressionRenderSeedWrappingBuffer(&editedTab);

    // Initial full build: 4 rows (1 + 2 wrapped + 1).
    Tab_UpdateVisualRows((const Editor*)NULL, &editedTab, usableColumns);
    assert(Array_Size(&editedTab.visualRows) == 4);

    // Three edits, no visual-rows update in between, then one update: by
    // then only the last edit's rebuild range is still recorded.
    RegressionRenderApplyUnsyncedEditBurst(&editedTab);
    Tab_UpdateVisualRows((const Editor*)NULL, &editedTab, usableColumns);

    // Ground truth: a second tab receives the identical content and edit
    // script, but its FIRST visual-rows build happens after all edits, so it
    // is a full from-scratch rebuild of the same final document.
    Tab groundTruthTab;
    Tab_Init(&groundTruthTab);
    RegressionRenderSeedWrappingBuffer(&groundTruthTab);
    RegressionRenderApplyUnsyncedEditBurst(&groundTruthTab);
    Tab_UpdateVisualRows((const Editor*)NULL, &groundTruthTab, usableColumns);

    // Expected final layout (verified against the full-rebuild wrapper):
    //   {0,0,10,false} {0,10,6,true} {1,0,8,false} {2,0,8,false} {3,0,5,false}
    RegressionRenderAssertVisualRowsIdentical(&editedTab.visualRows, &groundTruthTab.visualRows);

    Tab_Free(&groundTruthTab);
    Tab_Free(&editedTab);
}

// ---------------------------------------------------------------------------
// Editor_DrawTabsBar must bound its filename copies to the destination -- an
// unbounded memcpy of up to currentBlockWidth bytes into a fixed 256-byte
// stack buffer overflowed on wide terminals. On a wide terminal (2000 cols /
// 2 tabs => ~1000-byte block) a 500-byte filename takes the "fits entirely"
// copy path, and the fitting full path must be rendered into the tabs bar.
// ---------------------------------------------------------------------------
TEST(regression_render, tabs_bar_wide_terminal_no_overflow)
{
    TestPlatform testPlatform;
    TestPlatform_Init(&testPlatform);
    testPlatform.rows = 24;
    testPlatform.columns = 2000;

    Editor editor;
    Editor_Init(&editor);
    TestPlatform_Attach(&testPlatform, &editor);

    // Tabs bar is only drawn when more than one tab is open.
    Editor_AddTab(&editor, NULL);
    Editor_AddTab(&editor, NULL);
    Editor_UpdateGeometry(&editor);
    assert(editor.screenColumns == 2000);

    // 500-byte filenames: longer than any plausible fixed stack buffer but
    // shorter than the ~1000-byte block width (so the "name fits" copy path
    // is taken).
    // Tab_Free frees tab->filename, so strdup'd ownership transfers cleanly.
    char longPath[512];
    memset(longPath, 'a', sizeof(longPath) - 1);
    longPath[sizeof(longPath) - 1] = '\0';
    static const char pathPrefix[] = "/neoWideTabOverflowProbe/";
    memcpy(longPath, pathPrefix, sizeof(pathPrefix) - 1);
    longPath[500] = '\0';
    assert(strlen(longPath) == 500);

    for (size_t tabIndex = 0; tabIndex < 2; tabIndex++) {
        Tab* tab = Array_Get(&editor.tabs, Tab*, tabIndex);
        tab->filename = strdup(longPath);
    }

    Editor_RefreshScreen(&editor);

    // The full path fits its block, so the frame must contain it.
    assert(TestScreen_Contains(&testPlatform, "neoWideTabOverflowProbe"));
    assert(true);

    Editor_Free(&editor);
    TestPlatform_Free(&testPlatform);
}

// ---------------------------------------------------------------------------
// Editor_ScrollTab must guard degenerate geometry. With screenRows == 0 the
// scroll condition `cursorVRowIdx >= rowOffset + screenRows` is always true,
// and the unguarded assignment rowOffset = cursorVRowIdx - screenRows + 1 =
// cursorVRowIdx + 1 scrolls the viewport PAST the cursor, wrapping the
// cursor-position arithmetic in Editor_RefreshScreen through SIZE_MAX.
// rowOffset must never exceed the cursor's visual row and no wrapped-to-huge
// number may reach the escape codes.
// ---------------------------------------------------------------------------
TEST(regression_render, scroll_tiny_terminal_no_underflow)
{
    TestPlatform testPlatform;
    TestPlatform_Init(&testPlatform);
    testPlatform.rows = 2; // tiny terminal; bars eat every row
    testPlatform.columns = 80;

    Editor editor;
    Editor_Init(&editor);
    TestPlatform_Attach(&testPlatform, &editor);
    Editor_AddTab(&editor, NULL);
    Editor_UpdateGeometry(&editor);
    // Force the degenerate case deterministically.
    editor.screenRows = 0;

    Tab* tab = Array_Get(&editor.tabs, Tab*, editor.activeTabIndex);
    static const char fileText[] = "one\ntwo\nthree\nfour";
    Buffer_InsertText(tab->buffer, 0, fileText, sizeof(fileText) - 1);
    tab->cursorY = 2;
    tab->cursorX = 0;

    Editor_ScrollTab(&editor, tab);
    Editor_RefreshScreen(&editor);

    // The viewport must not scroll past the cursor's visual row...
    assert(tab->rowOffset <= Tab_GetCursorVRowIdx(tab));
    // ...and no SIZE_MAX-magnitude coordinate may leak into the frame.
    assert(!TestScreen_Contains(&testPlatform, "18446744073709551"));

    Editor_Free(&editor);
    TestPlatform_Free(&testPlatform);
}

// ---------------------------------------------------------------------------
// Editor_DrawTabRows must show the welcome banner only for a truly empty,
// unnamed buffer. A bare `totalVRows <= 1` condition also matches a real
// file whose content is exactly one line.
// ---------------------------------------------------------------------------
TEST(regression_render, welcome_banner_absent_for_one_line_file)
{
    TestPlatform testPlatform;
    TestPlatform_Init(&testPlatform); // 24 x 80 default

    Editor editor;
    Editor_Init(&editor);
    TestPlatform_Attach(&testPlatform, &editor);
    Editor_AddTab(&editor, NULL);
    Editor_UpdateGeometry(&editor);

    Tab* tab = Array_Get(&editor.tabs, Tab*, editor.activeTabIndex);
    Buffer_InsertText(tab->buffer, 0, "content", 7); // one line, no newline

    Editor_RefreshScreen(&editor);

    // The document's text must render...
    assert(TestScreen_Contains(&testPlatform, "content"));
    // ...and a non-empty buffer must NOT show the welcome banner.
    assert(!TestScreen_Contains(&testPlatform, "Neo Text Editor"));

    Editor_Free(&editor);
    TestPlatform_Free(&testPlatform);
}

// Companion behavior: a fresh empty scratch tab DOES show the welcome
// banner.
TEST(regression_render, welcome_banner_present_for_empty_scratch)
{
    TestPlatform testPlatform;
    TestPlatform_Init(&testPlatform); // 24 x 80 default

    Editor editor;
    Editor_Init(&editor);
    TestPlatform_Attach(&testPlatform, &editor);
    Editor_AddTab(&editor, NULL);
    Editor_UpdateGeometry(&editor);

    Editor_RefreshScreen(&editor);

    assert(TestScreen_Contains(&testPlatform, "Neo Text Editor"));

    Editor_Free(&editor);
    TestPlatform_Free(&testPlatform);
}

// ---------------------------------------------------------------------------
// Editor_DrawMessageBar must truncate the status message on codepoint
// boundaries -- cutting at screenColumns BYTES splits multi-byte UTF-8
// sequences at the cut point and emits an orphaned lead byte followed by
// ASCII padding (mojibake).
//
// Message: 1 ASCII byte + 40 x U+00E9 = 81 bytes. At 30 columns a byte cut
// falls after the 30th byte, which is the 0xC3 lead byte of the 15th pair.
// ---------------------------------------------------------------------------
TEST(regression_render, status_message_truncation_keeps_utf8_valid)
{
    TestPlatform testPlatform;
    TestPlatform_Init(&testPlatform);
    testPlatform.rows = 24;
    testPlatform.columns = 30;

    Editor editor;
    Editor_Init(&editor);
    TestPlatform_Attach(&testPlatform, &editor);
    Editor_AddTab(&editor, NULL);
    Editor_UpdateGeometry(&editor);
    assert(editor.screenColumns == 30);

    char longUtf8Message[128];
    RegressionRenderBuildEacuteRun(longUtf8Message, sizeof(longUtf8Message), '!', 40);
    Editor_SetStatusMessage(&editor, "%s", longUtf8Message);

    Editor_RefreshScreen(&editor);

    assert(RegressionRenderFrameKeepsTwoByteUtf8Intact(&testPlatform));

    Editor_Free(&editor);
    TestPlatform_Free(&testPlatform);
}

// Same requirement, second path: Editor_DrawStatusBar's filename cut, where
// a byte-precision "%.50s" splits codepoints. Filename: 1 ASCII byte +
// 30 x U+00E9 = 61 bytes; a 50-byte cut lands on the 0xC3 lead byte of the
// 25th pair, leaving an orphaned lead byte followed by " ~ N lines".
TEST(regression_render, status_bar_filename_truncation_keeps_utf8_valid)
{
    TestPlatform testPlatform;
    TestPlatform_Init(&testPlatform); // 24 x 80 default

    Editor editor;
    Editor_Init(&editor);
    TestPlatform_Attach(&testPlatform, &editor);
    Editor_AddTab(&editor, NULL);
    Editor_UpdateGeometry(&editor);

    char utf8Filename[80];
    RegressionRenderBuildEacuteRun(utf8Filename, sizeof(utf8Filename), 'x', 30);
    Tab* tab = Array_Get(&editor.tabs, Tab*, editor.activeTabIndex);
    tab->filename = strdup(utf8Filename); // freed by Tab_Free

    Editor_RefreshScreen(&editor);

    assert(RegressionRenderFrameKeepsTwoByteUtf8Intact(&testPlatform));

    Editor_Free(&editor);
    TestPlatform_Free(&testPlatform);
}

// ---------------------------------------------------------------------------
// Editor_ProcessExplorerInput, ARROW_DOWN in an EMPTY explorer: the bound
// `explorerSelectedIndex < numItems - 1` underflows to SIZE_MAX when
// numItems == 0, letting the selection index grow without bound. The empty
// list must be guarded so the index stays at 0.
// ---------------------------------------------------------------------------
TEST(regression_render, explorer_empty_list_arrow_down_no_underflow)
{
    Editor editor;
    Editor_Init(&editor);

    editor.isExplorerActive = true;
    assert(Array_Size(&editor.explorerItems) == 0); // never called Editor_ReadDir
    assert(editor.explorerSelectedIndex == 0);

    Editor_ProcessExplorerInput(&editor, ARROW_DOWN);

    assert(editor.explorerSelectedIndex == 0);

    Editor_Free(&editor);
}

// ---------------------------------------------------------------------------
// Editor_ToggleLogs appends its own LOG_INFO("Opened Log Viewer.") message;
// computing logsSelectedIndex = numLogs - 1 BEFORE that append opens the
// viewer with the second-to-last message selected. Opening the log viewer
// must select the true last message.
// ---------------------------------------------------------------------------
TEST(regression_render, logs_open_selects_last_message)
{
    Logger_Init("test_regression_render_unused.log", LOG_LEVEL_INFO, false, true, 50);

    Editor editor;
    Editor_Init(&editor);

    LOG_INFO("first message for the log viewer");
    LOG_INFO("second message for the log viewer");

    Editor_ToggleLogs(&editor);
    assert(editor.isLogsActive == true);

    Array* logMessages = Logger_GetMessages();
    assert(logMessages != NULL);
    assert(Array_Size(logMessages) > 0);
    assert(editor.logsSelectedIndex == Array_Size(logMessages) - 1);

    Editor_Free(&editor);
    Logger_Free();
}

// Tab_Init returning early when Buffer_New() fails (leaving the Tab
// uninitialized) is intentionally NOT covered here: it is unreachable
// without malloc failure injection.
