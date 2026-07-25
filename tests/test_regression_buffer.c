// Regression tests for buffer behavior: undo/redo across saves, line-cache
// re-splicing, render checkpoints on huge lines, size-field widths, and
// save edge cases.
//
// This file is #included into tests/main.c (single translation unit); all
// helpers are static and prefixed with RegressionBuffer to avoid collisions.

#include "../src/core/buffer.h"
#include "../src/core/history.h"
#include "../src/core/line.h"
#include "../src/utils/array.h"
#include "../src/utils/file_io.h"
#include "../src/utils/slice.h"
#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// ============================================================================
// Helpers
// ============================================================================

// Writes `content` to `path` and opens it as a file-backed (mmap) buffer,
// the same fixture pattern test_buffer.c uses for buffer_piece_table and
// buffer_save_fsync_options.
static Buffer* RegressionBufferCreateFileBacked(const char* path, const char* content)
{
    assert(FileIoWrite(path, Slice_From(content)));
    MappedFile mapped = FileIoMmap(path);
    assert(mapped.fileDescriptor != -1);
    Buffer* buffer = Buffer_NewFromMmap(mapped, path);
    assert(buffer != NULL);
    return buffer;
}

// Asserts the buffer's full content, total byte count, line count, and every
// individual line's text against `expected`.
static void RegressionBufferAssertContentEquals(Buffer* buffer, const char* expected)
{
    size_t expectedSize = strlen(expected);
    assert(Buffer_GetTotalBytes(buffer) == expectedSize);

    Slice whole = Buffer_ToSlice(buffer);
    assert(whole.size == expectedSize);
    assert(expectedSize == 0 || memcmp(whole.data, expected, expectedSize) == 0);
    free((void*)whole.data);

    size_t expectedLineCount = 1;
    for (const char* cursor = expected; *cursor; cursor++) {
        if (*cursor == '\n')
            expectedLineCount++;
    }
    assert(Buffer_GetLineCount(buffer) == expectedLineCount);

    const char* lineStart = expected;
    for (size_t lineIndex = 0; lineIndex < expectedLineCount; lineIndex++) {
        const char* newline = strchr(lineStart, '\n');
        size_t expectedLength = newline ? (size_t)(newline - lineStart) : strlen(lineStart);

        Line* line = Buffer_GetLine(buffer, lineIndex);
        assert(line != NULL);
        Slice text = Line_GetText(line, buffer);
        assert(text.size == expectedLength);
        assert(expectedLength == 0 || memcmp(text.data, lineStart, expectedLength) == 0);

        lineStart = newline ? newline + 1 : lineStart + expectedLength;
    }
}

// Asserts one cached line's offset, length, and text.
static void RegressionBufferAssertLine(
    Buffer* buffer, size_t lineIndex, size_t expectedOffset, const char* expectedText)
{
    Line* line = Buffer_GetLine(buffer, lineIndex);
    assert(line != NULL);
    assert(line->offset == expectedOffset);
    size_t expectedLength = strlen(expectedText);
    assert(line->length == expectedLength);
    Slice text = Line_GetText(line, buffer);
    assert(text.size == expectedLength);
    assert(expectedLength == 0 || memcmp(text.data, expectedText, expectedLength) == 0);
}

// Re-reads `path` from disk and asserts its exact content.
static void RegressionBufferAssertFileEquals(const char* path, const char* expected)
{
    Array fileContent = { 0 };
    assert(FileIoRead(path, &fileContent));
    assert(fileContent.size == strlen(expected));
    assert(fileContent.size == 0 || memcmp(fileContent.data, expected, fileContent.size) == 0);
    Array_Free(&fileContent);
}

// Reference tab-expansion walk mirroring Line_GetRenderX's expansion rule.
static size_t RegressionBufferBruteForceRenderX(const char* text, size_t targetColumn, size_t tabSize)
{
    size_t renderColumn = 0;
    for (size_t i = 0; i < targetColumn; i++) {
        if (text[i] == '\t')
            renderColumn += (tabSize - 1) - (renderColumn % tabSize);
        renderColumn++;
    }
    return renderColumn;
}

// ============================================================================
// Undo/redo across a save must restore the correct content. Buffer_OnSave
// swaps in a fresh mmap of the just-saved file, but undo/redo
// DocumentSnapshots still hold Pieces referencing the pre-save mapping and
// the add buffer -- so old mappings must stay alive on a buffer-owned
// retired list and the add buffer must never be cleared on save.
// ============================================================================

TEST(regression_buffer, save_then_undo)
{
    char directoryPath[] = "/tmp/neo_test_undo_XXXXXX";
    assert(mkdtemp(directoryPath) != NULL);
    char filePath[PATH_MAX];
    snprintf(filePath, sizeof(filePath), "%s/fixture.txt", directoryPath);

    Buffer* buffer = RegressionBufferCreateFileBacked(filePath, "one\ntwo\nthree\n");

    // One coalesced insert group ("XY" at line 0, column 0).
    Buffer_InsertChar(buffer, 0, 0, 'X');
    Buffer_InsertChar(buffer, 0, 1, 'Y');
    RegressionBufferAssertContentEquals(buffer, "XYone\ntwo\nthree\n");

    Buffer_OnSave(buffer, filePath, false);
    assert(buffer->isModified == false);

    // Undo across the save must restore the original fixture: the
    // snapshot's ORIGINAL pieces reference the pre-save mapping, which has
    // to remain alive and resolvable after the save.
    size_t undoLine = SIZE_MAX, undoColumn = SIZE_MAX;
    assert(Buffer_Undo(buffer, &undoLine, &undoColumn) == true);
    RegressionBufferAssertContentEquals(buffer, "one\ntwo\nthree\n");

    Buffer_Free(buffer);
    unlink(filePath);
    rmdir(directoryPath);
}

TEST(regression_buffer, save_then_redo)
{
    char directoryPath[] = "/tmp/neo_test_redo_XXXXXX";
    assert(mkdtemp(directoryPath) != NULL);
    char filePath[PATH_MAX];
    snprintf(filePath, sizeof(filePath), "%s/fixture.txt", directoryPath);

    Buffer* buffer = RegressionBufferCreateFileBacked(filePath, "one\ntwo\nthree\n");

    // Two separate groups (different lines), so one undo leaves the first
    // edit applied -- the save then writes content that differs from the old
    // mapping, which is what exercises snapshotAfter pieces that still
    // reference that old mapping.
    Buffer_InsertChar(buffer, 0, 0, 'A');
    Buffer_InsertChar(buffer, 1, 0, 'B');
    RegressionBufferAssertContentEquals(buffer, "Aone\nBtwo\nthree\n");

    size_t line = SIZE_MAX, column = SIZE_MAX;
    assert(Buffer_Undo(buffer, &line, &column) == true);
    RegressionBufferAssertContentEquals(buffer, "Aone\ntwo\nthree\n");

    Buffer_OnSave(buffer, filePath, false);
    assert(buffer->isModified == false);

    // Redo across the save must reproduce the post-edit content.
    assert(Buffer_Redo(buffer, &line, &column) == true);
    RegressionBufferAssertContentEquals(buffer, "Aone\nBtwo\nthree\n");

    Buffer_Free(buffer);
    unlink(filePath);
    rmdir(directoryPath);
}

TEST(regression_buffer, multiple_save_cycles)
{
    char directoryPath[] = "/tmp/neo_test_cycles_XXXXXX";
    assert(mkdtemp(directoryPath) != NULL);
    char filePath[PATH_MAX];
    snprintf(filePath, sizeof(filePath), "%s/fixture.txt", directoryPath);

    Buffer* buffer = RegressionBufferCreateFileBacked(filePath, "one\ntwo\nthree\n");

    const char* expectedStates[4] = {
        "one\ntwo\nthree\n",
        "1one\ntwo\nthree\n",
        "21one\ntwo\nthree\n",
        "321one\ntwo\nthree\n",
    };

    // Three rounds of (insert distinct marker at line 0 column 0, save).
    // Each insert lands at column 0 again, so each becomes its own group.
    for (int round = 1; round <= 3; round++) {
        Buffer_InsertChar(buffer, 0, 0, (char)('0' + round));
        Buffer_OnSave(buffer, filePath, false);
        RegressionBufferAssertContentEquals(buffer, expectedStates[round]);
        RegressionBufferAssertFileEquals(filePath, expectedStates[round]);
    }

    // Step back through every intermediate state...
    size_t line = SIZE_MAX, column = SIZE_MAX;
    for (int round = 3; round >= 1; round--) {
        assert(Buffer_Undo(buffer, &line, &column) == true);
        RegressionBufferAssertContentEquals(buffer, expectedStates[round - 1]);
    }

    // ...and forward again.
    for (int round = 1; round <= 3; round++) {
        assert(Buffer_Redo(buffer, &line, &column) == true);
        RegressionBufferAssertContentEquals(buffer, expectedStates[round]);
    }

    Buffer_Free(buffer);
    unlink(filePath);
    rmdir(directoryPath);
}

TEST(regression_buffer, edit_save_edit_save_undo_undo)
{
    char directoryPath[] = "/tmp/neo_test_esesuu_XXXXXX";
    assert(mkdtemp(directoryPath) != NULL);
    char filePath[PATH_MAX];
    snprintf(filePath, sizeof(filePath), "%s/fixture.txt", directoryPath);

    Buffer* buffer = RegressionBufferCreateFileBacked(filePath, "one\ntwo\nthree\n");

    Buffer_InsertChar(buffer, 0, 0, 'A'); // group 1
    Buffer_OnSave(buffer, filePath, false);
    Buffer_InsertChar(buffer, 0, 0, 'B'); // group 2 (column 0 again -> no coalescing)
    Buffer_OnSave(buffer, filePath, false);
    RegressionBufferAssertContentEquals(buffer, "BAone\ntwo\nthree\n");

    size_t line = SIZE_MAX, column = SIZE_MAX;
    assert(Buffer_Undo(buffer, &line, &column) == true);
    RegressionBufferAssertContentEquals(buffer, "Aone\ntwo\nthree\n");

    assert(Buffer_Undo(buffer, &line, &column) == true);
    RegressionBufferAssertContentEquals(buffer, "one\ntwo\nthree\n");

    // A final save of the fully-undone state must write the original
    // content to disk.
    Buffer_OnSave(buffer, filePath, false);
    RegressionBufferAssertFileEquals(filePath, "one\ntwo\nthree\n");

    Buffer_Free(buffer);
    unlink(filePath);
    rmdir(directoryPath);
}

// ============================================================================
// Undoing a MULTI-LINE composite group must re-splice the line cache
// correctly (line count, offsets, lengths). Buffer_RestoreSnapshot has to
// hand Buffer_InvalidateLineCache the true extent of the restored range;
// an understated range lets the rebuild falsely realign against the old
// cache tail and mis-splice the cache.
// ============================================================================

TEST(regression_buffer, undo_multiline_indent_group)
{
    Buffer* buffer = Buffer_New();
    Buffer_InsertText(buffer, 0, "aaa\nbbb\nccc\nddd", 15); // no trailing newline, 4 lines
    assert(Buffer_GetLineCount(buffer) == 4);

    // Mimic input.c's block indent: one force-grouped composite group that
    // inserts 4 spaces at column 0 of lines 0..2.
    buffer->history.forceGrouping = true;
    for (size_t lineIndex = 0; lineIndex < 3; lineIndex++) {
        for (int i = 0; i < 4; i++) {
            Buffer_InsertChar(buffer, lineIndex, 0, ' ');
        }
    }
    buffer->history.forceGrouping = false;
    buffer->history.currentGroup = NULL;

    RegressionBufferAssertContentEquals(buffer, "    aaa\n    bbb\n    ccc\nddd");
    assert(Buffer_GetTotalBytes(buffer) == 27);

    size_t line = SIZE_MAX, column = SIZE_MAX;
    assert(Buffer_Undo(buffer, &line, &column) == true);

    // The line cache must be correctly re-spliced.
    assert(Buffer_GetLineCount(buffer) == 4);
    RegressionBufferAssertLine(buffer, 0, 0, "aaa");
    RegressionBufferAssertLine(buffer, 1, 4, "bbb");
    RegressionBufferAssertLine(buffer, 2, 8, "ccc");
    RegressionBufferAssertLine(buffer, 3, 12, "ddd");
    assert(Buffer_GetTotalBytes(buffer) == 15);

    // Redo leg: the indented state must round-trip.
    assert(Buffer_Redo(buffer, &line, &column) == true);
    RegressionBufferAssertContentEquals(buffer, "    aaa\n    bbb\n    ccc\nddd");
    RegressionBufferAssertLine(buffer, 0, 0, "    aaa");
    RegressionBufferAssertLine(buffer, 1, 8, "    bbb");
    RegressionBufferAssertLine(buffer, 2, 16, "    ccc");
    RegressionBufferAssertLine(buffer, 3, 24, "ddd");

    Buffer_Free(buffer);
}

// Variant: one force-grouped group of multi-line DELETIONS. Note: a single
// boundary-crossing join round-trips correctly under the tail-shift
// heuristic (one change point => uniform tail shift), so this variant uses
// deletions at multiple disjoint points -- removing the indentation of
// lines 0..2 -- which is what actually stresses the cache re-splice.
TEST(regression_buffer, undo_multiline_delete_group)
{
    Buffer* buffer = Buffer_New();
    Buffer_InsertText(buffer, 0, "    aaa\n    bbb\n    ccc\nddd", 27);
    assert(Buffer_GetLineCount(buffer) == 4);

    buffer->history.forceGrouping = true;
    for (size_t lineIndex = 0; lineIndex < 3; lineIndex++) {
        for (int i = 0; i < 4; i++) {
            Buffer_DeleteChar(buffer, lineIndex, 0);
        }
    }
    buffer->history.forceGrouping = false;
    buffer->history.currentGroup = NULL;

    RegressionBufferAssertContentEquals(buffer, "aaa\nbbb\nccc\nddd");
    assert(Buffer_GetTotalBytes(buffer) == 15);

    size_t line = SIZE_MAX, column = SIZE_MAX;
    assert(Buffer_Undo(buffer, &line, &column) == true);

    assert(Buffer_GetLineCount(buffer) == 4);
    RegressionBufferAssertLine(buffer, 0, 0, "    aaa");
    RegressionBufferAssertLine(buffer, 1, 8, "    bbb");
    RegressionBufferAssertLine(buffer, 2, 16, "    ccc");
    RegressionBufferAssertLine(buffer, 3, 24, "ddd");
    assert(Buffer_GetTotalBytes(buffer) == 27);

    assert(Buffer_Redo(buffer, &line, &column) == true);
    assert(Buffer_GetLineCount(buffer) == 4);
    RegressionBufferAssertLine(buffer, 0, 0, "aaa");
    RegressionBufferAssertLine(buffer, 1, 4, "bbb");
    RegressionBufferAssertLine(buffer, 2, 8, "ccc");
    RegressionBufferAssertLine(buffer, 3, 12, "ddd");

    Buffer_Free(buffer);
}

// ============================================================================
// Line_GetRenderX on an edited huge line must reflect the edited content.
// Because Line_EnsureRenderCheckpoints never rebuilds when the checkpoint
// pointer is non-NULL and tabSize matches, the line-cache rebuild must not
// carry renderCheckpoints verbatim onto a replacement line whose content
// changed -- stale checkpoints would yield wrong render columns.
// ============================================================================

TEST(regression_buffer, huge_line_stale_render_checkpoints)
{
    const size_t hugeLength = 9200; // > LINE_HUGE_THRESHOLD (8192)
    assert(hugeLength > LINE_HUGE_THRESHOLD);
    const size_t tabSize = 4;

    // Model of the line's content, kept in lockstep with the buffer:
    // a tab every 500 characters, 'a' everywhere else.
    char* model = malloc(hugeLength + 1);
    assert(model != NULL);
    for (size_t i = 0; i < hugeLength; i++) {
        model[i] = (i % 500 == 0) ? '\t' : 'a';
    }

    Buffer* buffer = Buffer_New();
    Buffer_InsertText(buffer, 0, model, hugeLength);

    Line* line = Buffer_GetLine(buffer, 0);
    assert(line != NULL);
    assert(line->length == hugeLength);

    // First query on the huge line forces the checkpoint build; fresh
    // checkpoints must match a brute-force tab-expansion walk.
    assert(Line_GetRenderX(line, buffer, 9000, tabSize)
        == RegressionBufferBruteForceRenderX(model, 9000, tabSize));

    // Edit the line: prepend one tab, updating the model identically.
    memmove(model + 1, model, hugeLength);
    model[0] = '\t';
    Buffer_InsertText(buffer, 0, "\t", 1);

    line = Buffer_GetLine(buffer, 0);
    assert(line != NULL);
    assert(line->length == hugeLength + 1);

    // A query past several checkpoints (0, 4096, 8192) must reflect the
    // edited content: checkpoints built before the edit must not survive
    // it, or the result would be off by the render shift the leading tab
    // introduces. (Column 8400 sits in a tab-free stretch, so a stale base
    // cannot coincidentally realign.)
    assert(Line_GetRenderX(line, buffer, 8400, tabSize)
        == RegressionBufferBruteForceRenderX(model, 8400, tabSize));

    free(model);
    Buffer_Free(buffer);
}

// ============================================================================
// Buffer_InsertLine with lineNumber > lineCount clamps to EOF (inserting
// "\n" there) and must return the line it actually created, not NULL from
// a lookup at the out-of-range index.
// ============================================================================

TEST(regression_buffer, insert_line_past_end_returns_line)
{
    Buffer* buffer = Buffer_New();
    assert(Buffer_GetLineCount(buffer) == 1);

    Line* line = Buffer_InsertLine(buffer, 5);
    assert(Buffer_GetLineCount(buffer) == 2);
    assert(line != NULL); // the created line, not a lookup at index 5
    assert(line == Buffer_GetLine(buffer, 1));

    Buffer_Free(buffer);
}

// ============================================================================
// Array/GapBuffer/Piece/Action size fields are size_t-wide, and
// ArrayComputeAllocationSize must neither truncate nor wrap.
// ============================================================================

TEST(regression_buffer, size_fields_are_size_t)
{
    _Static_assert(sizeof(((Array*)0)->size) == sizeof(size_t), "Array.size must be size_t-wide");
    _Static_assert(
        sizeof(((Array*)0)->capacity) == sizeof(size_t), "Array.capacity must be size_t-wide");
    _Static_assert(
        sizeof(((Array*)0)->itemSize) == sizeof(size_t), "Array.itemSize must be size_t-wide");
    _Static_assert(
        sizeof(((GapBuffer*)0)->gapStart) == sizeof(size_t), "GapBuffer.gapStart must be size_t-wide");
    _Static_assert(
        sizeof(((GapBuffer*)0)->gapEnd) == sizeof(size_t), "GapBuffer.gapEnd must be size_t-wide");
    _Static_assert(sizeof(((Piece*)0)->length) == sizeof(size_t), "Piece.length must be size_t-wide");
    _Static_assert(
        sizeof(((Action*)0)->lineNumber) == sizeof(size_t), "Action.lineNumber must be size_t-wide");
    _Static_assert(
        sizeof(((Action*)0)->column) == sizeof(size_t), "Action.column must be size_t-wide");
}

TEST(regression_buffer, array_allocation_size_no_truncation)
{
    // A capacity just past UINT32_MAX must not be truncated to 32 bits.
    size_t hugeCapacity = (size_t)UINT32_MAX + 2;
    assert(ArrayComputeAllocationSize(1, hugeCapacity, 64) > (size_t)UINT32_MAX);

    // itemSize=8 with capacity 1<<29 is exactly 4GiB -- must not wrap.
    assert(ArrayComputeAllocationSize(8, (size_t)1 << 29, 64) == ((size_t)8 << 29));

    // Overflowing inputs must report failure (0), not a wrapped size.
    assert(ArrayComputeAllocationSize(SIZE_MAX, SIZE_MAX, 64) == 0);
    assert(ArrayComputeAllocationSize(2, SIZE_MAX / 2 + 1, 8) == 0);
    // Alignment round-up overflow must also report failure.
    assert(ArrayComputeAllocationSize(1, SIZE_MAX, 64) == 0);
}

// ============================================================================
// Buffer_OnSave's re-mmap of the just-saved file fails naturally for a
// 0-byte file (mmap of length 0 is EINVAL), exercising the failed-adoption
// path without any test seam. The buffer must stay fully usable afterward:
// editing and saving again after an empty save must produce exactly the
// new content on disk.
// ============================================================================

TEST(regression_buffer, save_empty_then_edit)
{
    char directoryPath[] = "/tmp/neo_test_empty_XXXXXX";
    assert(mkdtemp(directoryPath) != NULL);
    char filePath[PATH_MAX];
    snprintf(filePath, sizeof(filePath), "%s/fixture.txt", directoryPath);

    Buffer* buffer = RegressionBufferCreateFileBacked(filePath, "one\ntwo\nthree\n");

    // Delete everything, then save: the write succeeds, but the re-mmap of
    // the now 0-byte file fails.
    Buffer_DeleteRange(buffer, 0, Buffer_GetTotalBytes(buffer));
    assert(Buffer_GetTotalBytes(buffer) == 0);
    Buffer_OnSave(buffer, filePath, false);
    assert(buffer->isModified == false);
    RegressionBufferAssertFileEquals(filePath, "");

    // Edit after the empty save, save again, and verify the file on disk.
    Buffer_InsertText(buffer, 0, "abc", 3);
    RegressionBufferAssertContentEquals(buffer, "abc");
    Buffer_OnSave(buffer, filePath, false);
    RegressionBufferAssertFileEquals(filePath, "abc");
    RegressionBufferAssertContentEquals(buffer, "abc");

    Buffer_Free(buffer);
    unlink(filePath);
    rmdir(directoryPath);
}
