#include "../neo.h"
#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// LINE CACHE INTERNAL METHODS
// ============================================================================

static void LineCache_Init(LineCache* lc)
{
    GapBuffer_Init(&lc->lines, sizeof(Line), 0, alignof(Line));
    lc->dirtyLineStart = 0;
    lc->dirtyOffsetEnd = SIZE_MAX;
    lc->oldTotalBytes = 0;
}

static void LineCache_Free(LineCache* lc)
{
    if (!lc->lines.data.data)
        return;
    for (size_t i = 0; i < lc->lines.data.capacity; i++) {
        if (i < lc->lines.gapStart || i >= lc->lines.gapEnd) {
            Line* line = (Line*)Array_RawAt(&lc->lines.data, i);
            free(line->text);
            Array_Free(&line->styles);
            Array_Free(&line->testText);
            free(line->renderCheckpoints);
        }
    }
    GapBuffer_Free(&lc->lines);
}

static void LineCache_Insert(LineCache* lc, Line* line)
{
    GapBuffer_InsertSlice(&lc->lines, GapBuffer_Size(&lc->lines), Slice_Make(line, sizeof(Line)));
}

static Line* LineCache_At(const LineCache* lc, size_t index) { return (Line*)GapBuffer_At(&lc->lines, index); }

// ============================================================================
// PIECE TABLE HELPERS
// ============================================================================

static Piece* Buffer_GetPiece(const Buffer* buffer, size_t index)
{
    return (Piece*)GapBuffer_At(&buffer->pieces, index);
}

typedef struct {
    size_t pieceIndex;
    size_t localOffset;
} PiecePosition;

static PiecePosition Buffer_FindPiecePosition(const Buffer* buffer, size_t targetOffset)
{
    size_t currentOffset = 0;
    size_t pieceCount = GapBuffer_Size(&buffer->pieces);
    for (size_t i = 0; i < pieceCount; i++) {
        Piece* p = Buffer_GetPiece(buffer, i);
        if (targetOffset >= currentOffset && targetOffset < currentOffset + p->length) {
            return (PiecePosition) { .pieceIndex = i, .localOffset = targetOffset - currentOffset };
        }
        currentOffset += p->length;
    }
    return (PiecePosition) { .pieceIndex = pieceCount, .localOffset = 0 };
}

static Line* Buffer_GetCachedLine(Buffer* buffer, size_t lineNumber)
{
    return LineCache_At(&buffer->lineCache, lineNumber);
}

// ============================================================================
// LAZY REBUILDING SCANNERS
// ============================================================================

// Binary-searches old lines [rangeStart, rangeStart+rangeCount) directly on
// the live (not-yet-touched) line cache for one with the given offset.
static size_t FindOldLineIndexByOffset(const Buffer* buffer, size_t rangeStart, size_t rangeCount, size_t targetOffset)
{
    size_t low = 0;
    size_t high = rangeCount;
    while (low < high) {
        size_t mid = low + (high - low) / 2;
        size_t off = LineCache_At(&buffer->lineCache, rangeStart + mid)->offset;
        if (off == targetOffset) {
            return mid;
        } else if (off < targetOffset) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }
    return SIZE_MAX;
}

// Rebuilds the line cache from the dirty point forward. Scans (via memchr)
// only until it can realign with the existing cache's untouched tail -- at
// that point the tail is left exactly where it is (not copied out and
// reinserted), and only its offset/lineNumber fields are patched in place,
// since re-materializing every remaining line on every edit made editing cost
// O(lines after the edit) instead of O(lines actually touched).
static void Buffer_RebuildLineCache(Buffer* buffer, size_t upToLineIndex)
{
    if (buffer->lineCache.dirtyLineStart == SIZE_MAX) {
        return;
    }
    if (buffer->lineCache.dirtyLineStart > upToLineIndex && upToLineIndex != SIZE_MAX) {
        return;
    }

    size_t dirtyStart = buffer->lineCache.dirtyLineStart;
    size_t oldTotal = GapBuffer_Size(&buffer->lineCache.lines);
    size_t oldRangeCount = (oldTotal > dirtyStart) ? (oldTotal - dirtyStart) : 0;
    size_t delta = buffer->totalBytes - buffer->lineCache.oldTotalBytes;

    // Find starting offset for scanning, from the untouched line just before
    // the dirty range (still valid -- nothing before dirtyStart is touched).
    size_t currentOffset = 0;
    if (dirtyStart > 0) {
        Line* prevLine = LineCache_At(&buffer->lineCache, dirtyStart - 1);
        currentOffset = prevLine->offset + prevLine->length + 1; // +1 for the newline
    }

    PiecePosition pos = Buffer_FindPiecePosition(buffer, currentOffset);
    size_t pieceIdx = pos.pieceIndex;
    size_t localOffset = pos.localOffset;
    size_t pieceCount = GapBuffer_Size(&buffer->pieces);

    // Freshly-scanned replacement lines accumulate here -- NOT yet touching
    // buffer->lineCache.lines, so its untouched tail stays exactly where it
    // is (and remains valid to binary-search against) until we know exactly
    // how much of it we get to keep.
    Array newLines;
    Array_InitStruct(&newLines, Line, 4);

    size_t scanOffset = currentOffset;
    size_t lineStartOffset = currentOffset;
    size_t scanLineIdx = dirtyStart;
    bool aligned = false;
    size_t alignedOldIdx = SIZE_MAX; // old index (absolute) where the untouched tail begins

    while (pieceIdx < pieceCount && !aligned) {
        Piece* p = Buffer_GetPiece(buffer, pieceIdx);
        const char* src
            = (p->source == PIECE_SOURCE_ORIGINAL) ? buffer->bufferOriginal.data : (const char*)buffer->bufferAdd.data;
        const char* pieceText = src + p->start;

        size_t j = localOffset;
        while (j < p->length) {
            const char* nextNewline = memchr(pieceText + j, '\n', p->length - j);
            if (nextNewline) {
                size_t foundIdx = nextNewline - pieceText;
                size_t lineLen = (scanOffset + (foundIdx - j)) - lineStartOffset;
                Line newLine = { .buffer = buffer,
                    .offset = lineStartOffset,
                    .length = lineLen,
                    .text = NULL,
                    .lineNumber = scanLineIdx,
                    .isFolded = false,
                    .foldLevel = 0,
                    .styles = { 0 },
                    .testText = { 0 },
                    .renderCheckpoints = NULL,
                    .renderCheckpointCount = 0,
                    .renderCheckpointTabSize = 0 };
                Array_Append(&newLines, &newLine, 1);
                scanLineIdx++;
                scanOffset += (foundIdx - j) + 1;
                lineStartOffset = scanOffset;
                j = foundIdx + 1;

                // Check for alignment
                if (lineStartOffset >= buffer->lineCache.dirtyOffsetEnd && oldRangeCount > 0) {
                    size_t targetOldOffset = lineStartOffset - delta;
                    size_t k = FindOldLineIndexByOffset(buffer, dirtyStart, oldRangeCount, targetOldOffset);
                    if (k != SIZE_MAX) {
                        alignedOldIdx = dirtyStart + k;
                        aligned = true;
                        break;
                    }
                }
            } else {
                scanOffset += (p->length - j);
                break;
            }
        }
        pieceIdx++;
        localOffset = 0;
    }

    // Handle last line if we didn't align
    if (!aligned && lineStartOffset <= buffer->totalBytes) {
        size_t lineLen = buffer->totalBytes - lineStartOffset;
        Line newLine = { .buffer = buffer,
            .offset = lineStartOffset,
            .length = lineLen,
            .text = NULL,
            .lineNumber = scanLineIdx,
            .isFolded = false,
            .foldLevel = 0,
            .styles = { 0 },
            .testText = { 0 },
            .renderCheckpoints = NULL,
            .renderCheckpointCount = 0,
            .renderCheckpointTabSize = 0 };
        Array_Append(&newLines, &newLine, 1);
        scanLineIdx++;
    }

    size_t oldRangeEnd = aligned ? alignedOldIdx : oldTotal; // exclusive; old lines [dirtyStart, oldRangeEnd) are replaced
    size_t numOldReplaced = oldRangeEnd - dirtyStart;
    size_t numNew = Array_Size(&newLines);

    // Record the touched range so incremental consumers (e.g. visual row wrapping)
    // can splice just the affected span instead of rebuilding from scratch.
    buffer->lastRebuiltOldStart = dirtyStart;
    buffer->lastRebuiltOldEnd = oldRangeEnd;
    buffer->lastRebuiltNewEnd = dirtyStart + numNew;
    buffer->lastRebuildOccurred = true;

    // Copy back preserved fold/style/comment state where a freshly-scanned
    // line positionally correlates with an old one (aligning from the tail
    // of the replaced range, since a line-count-changing edit happens near
    // its start) -- scoped to [dirtyStart, oldRangeEnd), not the whole file.
    for (size_t i = dirtyStart; i < dirtyStart + numNew; i++) {
        size_t oldIdx = i - numNew + numOldReplaced;
        if (oldIdx >= dirtyStart && oldIdx < oldRangeEnd) {
            Line* oldLine = LineCache_At(&buffer->lineCache, oldIdx);
            Line* newLine = (Line*)Array_At(&newLines, i - dirtyStart);
            newLine->isFolded = oldLine->isFolded;
            newLine->foldLevel = oldLine->foldLevel;
            newLine->commentStateOut = oldLine->commentStateOut;
            newLine->commentStateOutValid = oldLine->commentStateOutValid;
            // Move styles array (shallow copy)
            Array_Free(&newLine->styles);
            newLine->styles = oldLine->styles;
            oldLine->styles = (Array) { 0 }; // ownership transferred; prevent double-free below
        }
    }

    // Free the old lines actually being replaced/discarded (styles already
    // transferred above where correlated). Scoped to the replaced range only
    // -- the untouched reused tail beyond oldRangeEnd is never touched here.
    size_t discardedFoldedCount = 0;
    for (size_t i = dirtyStart; i < oldRangeEnd; i++) {
        Line* oldLine = LineCache_At(&buffer->lineCache, i);
        if (oldLine->isFolded) {
            discardedFoldedCount++;
        }
        free(oldLine->text);
        Array_Free(&oldLine->styles);
        Array_Free(&oldLine->testText);
        free(oldLine->renderCheckpoints);
    }

    // Splice: replace old [dirtyStart, oldRangeEnd) with the freshly-scanned
    // lines. The untouched tail beyond oldRangeEnd is never moved out and
    // back in -- GapBuffer_Delete/InsertSlice only need to shift the gap
    // across it, not copy each line individually.
    if (numOldReplaced > 0) {
        GapBuffer_Delete(&buffer->lineCache.lines, dirtyStart, numOldReplaced);
    }
    if (numNew > 0) {
        GapBuffer_InsertSlice(&buffer->lineCache.lines, dirtyStart, Slice_Make(newLines.data, numNew * sizeof(Line)));
    }
    Array_Free(&newLines);

    // Patch the reused tail's absolute offsets (and line numbers, if the
    // total line count changed) in place -- no move, no per-line reinsert.
    size_t newTotal = GapBuffer_Size(&buffer->lineCache.lines);
    for (size_t i = dirtyStart + numNew; i < newTotal; i++) {
        Line* line = (Line*)GapBuffer_At(&buffer->lineCache.lines, i);
        line->offset += delta;
        line->lineNumber = i;
    }

    // Recompute folded lines in the modified range
    size_t newFolded = 0;
    for (size_t i = dirtyStart; i < dirtyStart + numNew; i++) {
        if (LineCache_At(&buffer->lineCache, i)->isFolded) {
            newFolded++;
        }
    }
    buffer->foldedLineCount = buffer->foldedLineCount - discardedFoldedCount + newFolded;

    buffer->lineCache.dirtyLineStart = SIZE_MAX;
    buffer->lineCache.dirtyOffsetEnd = SIZE_MAX;
}

void Buffer_InvalidateLineCache(Buffer* buffer, size_t offset, size_t newEndOffset, size_t oldTotalBytes)
{
    buffer->editVersion++;

    size_t lineIdx = 0;
    if (offset > 0 && buffer->lineCache.dirtyLineStart != 0) {
        size_t searchLimit = (buffer->lineCache.dirtyLineStart == SIZE_MAX) ? GapBuffer_Size(&buffer->lineCache.lines)
                                                                            : buffer->lineCache.dirtyLineStart;
        // Lines are ordered by ascending offset, so binary search for the first line
        // whose end reaches `offset` instead of scanning linearly (O(log n) vs O(n)).
        size_t low = 0, high = searchLimit;
        while (low < high) {
            size_t mid = low + (high - low) / 2;
            Line* line = LineCache_At(&buffer->lineCache, mid);
            if (line->offset + line->length < offset) {
                low = mid + 1;
            } else {
                high = mid;
            }
        }
        if (low < searchLimit) {
            lineIdx = low;
        }
    }

    if (buffer->lineCache.dirtyLineStart == SIZE_MAX) {
        buffer->lineCache.dirtyLineStart = lineIdx;
        buffer->lineCache.dirtyOffsetEnd = newEndOffset;
        buffer->lineCache.oldTotalBytes = oldTotalBytes;
    } else {
        if (lineIdx < buffer->lineCache.dirtyLineStart) {
            buffer->lineCache.dirtyLineStart = lineIdx;
        }
        // A second edit landed before the pending dirty range was ever rebuilt, so
        // dirtyOffsetEnd (recorded in the first edit's coordinate space) can no longer
        // be trusted as a byte offset in the current buffer. Disable the realignment
        // fast path for this rebuild instead of risking reusing shifted-but-stale lines.
        buffer->lineCache.dirtyOffsetEnd = SIZE_MAX;
    }
}

// ============================================================================
// DEFRAGMENTATION
// ============================================================================

static void Buffer_Defragment(Buffer* buffer)
{
    size_t pieceCount = GapBuffer_Size(&buffer->pieces);
    if (pieceCount <= 1)
        return;

    GapBuffer newPieces;
    GapBuffer_Init(&newPieces, sizeof(Piece), pieceCount, alignof(Piece));

    Piece prev = *(Piece*)GapBuffer_At(&buffer->pieces, 0);
    for (size_t i = 1; i < pieceCount; i++) {
        Piece curr = *(Piece*)GapBuffer_At(&buffer->pieces, i);
        if (prev.source == curr.source && prev.start + prev.length == curr.start) {
            prev.length += curr.length;
        } else {
            GapBuffer_InsertSlice(&newPieces, GapBuffer_Size(&newPieces), Slice_Make(&prev, sizeof(Piece)));
            prev = curr;
        }
    }
    GapBuffer_InsertSlice(&newPieces, GapBuffer_Size(&newPieces), Slice_Make(&prev, sizeof(Piece)));

    GapBuffer_Free(&buffer->pieces);
    buffer->pieces = newPieces;
}

// ============================================================================
// UNDO / REDO SNAPSHOTS
// ============================================================================

DocumentSnapshot* DocumentSnapshot_Copy(Buffer* buffer)
{
    LOG_DEBUG(
        "DocumentSnapshot_Copy: creating snapshot of buffer %p (totalBytes=%zu)", (void*)buffer, buffer->totalBytes);
    DocumentSnapshot* snap = malloc(sizeof(DocumentSnapshot));
    assert(snap);

    snap->totalBytes = buffer->totalBytes;

    // A gap buffer's real pieces are split into a before-gap segment and an
    // after-gap segment, not contiguous from the start of the array, so each
    // must be copied separately.
    size_t pieceCount = GapBuffer_Size(&buffer->pieces);
    GapBuffer_Init(&snap->pieces, sizeof(Piece), pieceCount, alignof(Piece));
    GapBuffer_InsertSlice(
        &snap->pieces, 0, Slice_Make(buffer->pieces.data.data, buffer->pieces.gapStart * sizeof(Piece)));
    GapBuffer_InsertSlice(&snap->pieces, GapBuffer_Size(&snap->pieces),
        Slice_Make((char*)buffer->pieces.data.data + buffer->pieces.gapEnd * sizeof(Piece),
            (pieceCount - buffer->pieces.gapStart) * sizeof(Piece)));

    return snap;
}

void DocumentSnapshot_Free(DocumentSnapshot* snap)
{
    if (!snap)
        return;
    LOG_DEBUG("DocumentSnapshot_Free: freeing snapshot %p (totalBytes=%zu)", (void*)snap, snap->totalBytes);
    GapBuffer_Free(&snap->pieces);
    free(snap);
}

void Buffer_RestoreSnapshot(Buffer* buffer, DocumentSnapshot* snap, size_t rangeStartOffset)
{
    assert(buffer && snap);
    LOG_DEBUG("Buffer_RestoreSnapshot: restoring snapshot %p to buffer %p (totalBytes=%zu)", (void*)snap, (void*)buffer,
        snap->totalBytes);

    size_t oldTotalBytes = buffer->totalBytes;

    GapBuffer_Free(&buffer->pieces);

    // See DocumentSnapshot_Copy: the snapshot's pieces are likewise split
    // around its own gap, so copy each segment separately.
    size_t pieceCount = GapBuffer_Size(&snap->pieces);
    GapBuffer_Init(&buffer->pieces, sizeof(Piece), pieceCount, alignof(Piece));
    GapBuffer_InsertSlice(
        &buffer->pieces, 0, Slice_Make(snap->pieces.data.data, snap->pieces.gapStart * sizeof(Piece)));
    GapBuffer_InsertSlice(&buffer->pieces, GapBuffer_Size(&buffer->pieces),
        Slice_Make((char*)snap->pieces.data.data + snap->pieces.gapEnd * sizeof(Piece),
            (pieceCount - snap->pieces.gapStart) * sizeof(Piece)));

    buffer->totalBytes = snap->totalBytes;

    if (rangeStartOffset == SIZE_MAX || rangeStartOffset > oldTotalBytes) {
        // No valid lower bound on the touched region is known (e.g. an empty
        // action group) - fall back to discarding the whole line cache, safe
        // if not incremental.
        LineCache_Free(&buffer->lineCache);
        LineCache_Init(&buffer->lineCache);
        buffer->foldedLineCount = 0;
        buffer->editVersion++;
    } else {
        // Every action group's actions share a single lineNumber (see
        // CanGroupActions), and undo/redo's LIFO discipline guarantees the
        // buffer is in exactly the state the group last left it in when this
        // runs - so everything before that line is byte-identical between
        // the current buffer and the restored snapshot. Reuse the same
        // incremental invalidation ordinary edits use (Buffer_DeleteRange
        // does the same zero-width-range call) instead of discarding every
        // cached line.
        Buffer_InvalidateLineCache(buffer, rangeStartOffset, rangeStartOffset, oldTotalBytes);
    }
}

// ============================================================================
// EDITING TRANSACTION GROUPING
// ============================================================================

static bool CanGroupActions(Action* last, Action* next)
{
    if (last->type != next->type)
        return false;

    if (last->type == ACTION_INSERT_CHAR) {
        return (last->lineNumber == next->lineNumber && last->column + 1 == next->column);
    }

    if (last->type == ACTION_DELETE_CHAR) {
        return (
            last->lineNumber == next->lineNumber && (last->column - 1 == next->column || last->column == next->column));
    }

    return false;
}

static void RecordAction(Buffer* buffer, Action action)
{
    if (buffer->history.isUndoRedoing)
        return;

    Stack_Free(&buffer->history.redoStack, (void (*)(void*))ActionGroup_Free);

    ActionGroup* current = buffer->history.currentGroup;
    if (current && Array_Size(&current->actions) > 0) {
        Action* lastAction = (Action*)Array_At(&current->actions, Array_Size(&current->actions) - 1);
        if (CanGroupActions(lastAction, &action)) {
            Array_Append(&current->actions, &action, 1);
            return;
        }
    }

    ActionGroup* group = ActionGroup_New();
    if (!group)
        return;

    group->snapshotBefore = DocumentSnapshot_Copy(buffer);
    group->snapshotAfter = NULL;

    Array_Append(&group->actions, &action, 1);

    buffer->history.currentGroup = group;
    Stack_Push(&buffer->history.undoStack, group);
    Stack_EnforceLimit(&buffer->history.undoStack, buffer->history.undoLimit, (void (*)(void*))ActionGroup_Free);
}

// ============================================================================
// PUBLIC BUFFER API
// ============================================================================

Buffer* Buffer_New(void)
{
    Buffer* buffer = calloc(1, sizeof(Buffer));
    if (!buffer)
        return NULL;

    buffer->refCount = 1;
    buffer->totalBytes = 0;
    buffer->mappedFile.fileDescriptor = -1;

    buffer->bufferOriginal = (Slice) { .data = NULL, .size = 0 };
    Array_Init(&buffer->bufferAdd, sizeof(char), 256, 0);

    GapBuffer_Init(&buffer->pieces, sizeof(Piece), 8, alignof(Piece));

    LineCache_Init(&buffer->lineCache);

    History_Init(&buffer->history);

    Line initialLine = { .buffer = buffer,
        .offset = 0,
        .length = 0,
        .text = NULL,
        .lineNumber = 0,
        .isFolded = false,
        .foldLevel = 0,
        .styles = { 0 },
        .testText = { 0 },
        .renderCheckpoints = NULL,
        .renderCheckpointCount = 0,
        .renderCheckpointTabSize = 0 };
    LineCache_Insert(&buffer->lineCache, &initialLine);
    buffer->lineCache.dirtyLineStart = SIZE_MAX;

    LOG_INFO("Buffer_New: Created empty buffer instance.");
    return buffer;
}

Buffer* Buffer_NewFromMmap(MappedFile mappedFile, const char* filename)
{
    Buffer* buffer = calloc(1, sizeof(Buffer));
    if (!buffer)
        return NULL;

    buffer->refCount = 1;
    buffer->mappedFile = mappedFile;
    buffer->bufferOriginal = mappedFile.content;
    buffer->totalBytes = mappedFile.content.size;
    buffer->filename = filename ? strdup(filename) : NULL;

    Array_Init(&buffer->bufferAdd, sizeof(char), 256, 0);

    GapBuffer_Init(&buffer->pieces, sizeof(Piece), 8, alignof(Piece));
    if (mappedFile.content.size > 0) {
        Piece firstPiece = { .source = PIECE_SOURCE_ORIGINAL, .start = 0, .length = mappedFile.content.size };
        GapBuffer_InsertSlice(&buffer->pieces, 0, Slice_Make(&firstPiece, sizeof(Piece)));
    }

    LineCache_Init(&buffer->lineCache);
    buffer->lineCache.dirtyLineStart = 0;
    buffer->lineCache.dirtyOffsetEnd = buffer->totalBytes;
    buffer->lineCache.oldTotalBytes = buffer->totalBytes;

    History_Init(&buffer->history);

    LOG_INFO("Buffer_NewFromMmap: Created memory-mapped buffer from file '%s' (bytes=%zu).",
        filename ? filename : "<unknown>", mappedFile.content.size);
    return buffer;
}

void Buffer_Free(Buffer* buffer)
{
    if (!buffer)
        return;

    buffer->refCount--;
    if (buffer->refCount > 0) {
        LOG_DEBUG("Buffer_Free: DecRef refCount to %d for '%s'", buffer->refCount,
            buffer->filename ? buffer->filename : "<none>");
        return;
    }

    LOG_INFO("Buffer_Free: Destroying buffer for file '%s'.", buffer->filename ? buffer->filename : "<none>");

    MappedFile_Unmap(&buffer->mappedFile);
    Array_Free(&buffer->bufferAdd);
    GapBuffer_Free(&buffer->pieces);
    LineCache_Free(&buffer->lineCache);
    History_Free(&buffer->history);
    free(buffer->filename);
    free(buffer);
}

Line* Buffer_GetLine(const Buffer* buffer, size_t lineNumber)
{
    assert(buffer);
    Buffer* mutBuffer = (Buffer*)buffer;

    if (mutBuffer->lineCache.dirtyLineStart != SIZE_MAX) {
        Buffer_RebuildLineCache(mutBuffer, SIZE_MAX);
    }

    if (lineNumber >= GapBuffer_Size(&mutBuffer->lineCache.lines)) {
        return NULL;
    }

    return Buffer_GetCachedLine(mutBuffer, lineNumber);
}

Line* Buffer_InsertLine(Buffer* buffer, size_t lineNumber)
{
    assert(buffer);
    LOG_DEBUG(
        "Buffer_InsertLine: inserting line at index %zu (totalLines=%zu)", lineNumber, Buffer_GetLineCount(buffer));
    size_t offset = 0;
    size_t totalLines = Buffer_GetLineCount(buffer);
    if (lineNumber > 0) {
        if (lineNumber >= totalLines) {
            offset = buffer->totalBytes;
        } else {
            Line* line = Buffer_GetLine(buffer, lineNumber);
            offset = line->offset;
        }
    }

    Buffer_InsertText(buffer, offset, "\n", 1);
    buffer->isModified = true;

    return Buffer_GetLine(buffer, lineNumber);
}

void Buffer_DeleteLine(Buffer* buffer, size_t lineNumber)
{
    assert(buffer);
    LOG_DEBUG(
        "Buffer_DeleteLine: deleting line at index %zu (totalLines=%zu)", lineNumber, Buffer_GetLineCount(buffer));
    Line* line = Buffer_GetLine(buffer, lineNumber);
    if (!line)
        return;

    size_t start = line->offset;
    size_t len = line->length;

    if (start + len < buffer->totalBytes) {
        Buffer_DeleteRange(buffer, start, start + len + 1);
    } else if (start > 0) {
        Buffer_DeleteRange(buffer, start - 1, start + len);
    } else {
        Buffer_DeleteRange(buffer, start, start + len);
    }
    buffer->isModified = true;
}

static size_t Buffer_GetAbsoluteOffset(Buffer* buffer, size_t lineNumber, size_t column)
{
    Line* line = Buffer_GetLine(buffer, lineNumber);
    if (!line)
        return buffer->totalBytes;
    if (column > line->length)
        column = line->length;
    return line->offset + column;
}

void Buffer_InsertChar(Buffer* buffer, size_t lineNumber, size_t column, char character)
{
    assert(buffer);
    LOG_DEBUG("Buffer_InsertChar: inserting '%c' at line=%zu col=%zu", character, lineNumber, column);
    size_t offset = Buffer_GetAbsoluteOffset(buffer, lineNumber, column);

    Action action = {
        .type = ACTION_INSERT_CHAR, .lineNumber = lineNumber, .column = column, .payload = { .character = character }
    };
    RecordAction(buffer, action);

    Buffer_InsertText(buffer, offset, &character, 1);
    buffer->isModified = true;
}

void Buffer_DeleteChar(Buffer* buffer, size_t lineNumber, size_t column)
{
    assert(buffer);
    LOG_DEBUG("Buffer_DeleteChar: deleting char at line=%zu col=%zu", lineNumber, column);
    Line* line = Buffer_GetLine(buffer, lineNumber);
    if (line && column < line->length) {
        char c = Line_GetChar(line, column);
        Action action
            = { .type = ACTION_DELETE_CHAR, .lineNumber = lineNumber, .column = column, .payload = { .character = c } };
        RecordAction(buffer, action);

        size_t offset = line->offset + column;
        Buffer_DeleteRange(buffer, offset, offset + 1);
        buffer->isModified = true;
    }
}

void Buffer_SplitLine(Buffer* buffer, size_t lineNumber, size_t column)
{
    assert(buffer);
    LOG_DEBUG("Buffer_SplitLine: splitting line=%zu col=%zu", lineNumber, column);
    Line* line = Buffer_GetLine(buffer, lineNumber);
    if (line) {
        if (column > line->length)
            column = line->length;
        Action action = { .type = ACTION_SPLIT_LINE, .lineNumber = lineNumber, .column = column };
        RecordAction(buffer, action);

        size_t offset = line->offset + column;
        Buffer_InsertText(buffer, offset, "\n", 1);
        buffer->isModified = true;
    }
}

void Buffer_JoinLine(Buffer* buffer, size_t lineNumber)
{
    assert(buffer);
    LOG_DEBUG("Buffer_JoinLine: joining line=%zu with line=%zu", lineNumber, lineNumber + 1);
    Line* line = Buffer_GetLine(buffer, lineNumber);
    if (line) {
        Action action = { .type = ACTION_JOIN_LINE, .lineNumber = lineNumber, .column = line->length };
        RecordAction(buffer, action);

        size_t offset = line->offset + line->length;
        if (offset < buffer->totalBytes) {
            Buffer_DeleteRange(buffer, offset, offset + 1);
            buffer->isModified = true;
        }
    }
}

void Buffer_InsertText(Buffer* buffer, size_t pos, const char* text, size_t len)
{
    if (len == 0)
        return;

    LOG_INFO("Buffer_InsertText: inserting %zu bytes at position %zu", len, pos);

    size_t currentOffset = 0;
    size_t pieceCount = GapBuffer_Size(&buffer->pieces);
    size_t targetPieceIdx = pieceCount;
    size_t localOffset = 0;

    for (size_t i = 0; i < pieceCount; i++) {
        Piece* p = Buffer_GetPiece(buffer, i);
        if (pos >= currentOffset && pos <= currentOffset + p->length) {
            targetPieceIdx = i;
            localOffset = pos - currentOffset;
            break;
        }
        currentOffset += p->length;
    }

    size_t addStart = buffer->bufferAdd.size;
    Array_Append(&buffer->bufferAdd, text, len);

    if (localOffset == 0 && targetPieceIdx > 0) {
        Piece* prevPiece = Buffer_GetPiece(buffer, targetPieceIdx - 1);
        if (prevPiece->source == PIECE_SOURCE_ADD && prevPiece->start + prevPiece->length == addStart) {
            prevPiece->length += len;
            buffer->totalBytes += len;
            Buffer_InvalidateLineCache(buffer, pos, pos + len, buffer->totalBytes - len);
            if (GapBuffer_Size(&buffer->pieces) > 1000) {
                Buffer_Defragment(buffer);
            }
            return;
        }
    } else if (targetPieceIdx < pieceCount) {
        Piece* targetPiece = Buffer_GetPiece(buffer, targetPieceIdx);
        if (localOffset == targetPiece->length) {
            if (targetPiece->source == PIECE_SOURCE_ADD && targetPiece->start + targetPiece->length == addStart) {
                targetPiece->length += len;
                buffer->totalBytes += len;
                Buffer_InvalidateLineCache(buffer, pos, pos + len, buffer->totalBytes - len);
                if (GapBuffer_Size(&buffer->pieces) > 1000) {
                    Buffer_Defragment(buffer);
                }
                return;
            }
        }
    }

    Piece newPiece = { .source = PIECE_SOURCE_ADD, .start = addStart, .length = len };

    if (targetPieceIdx < pieceCount && localOffset > 0
        && localOffset < Buffer_GetPiece(buffer, targetPieceIdx)->length) {
        Piece* p = Buffer_GetPiece(buffer, targetPieceIdx);
        Piece left = { .source = p->source, .start = p->start, .length = localOffset };
        Piece right = { .source = p->source, .start = p->start + localOffset, .length = p->length - localOffset };

        *p = left;

        GapBuffer_InsertSlice(&buffer->pieces, targetPieceIdx + 1, Slice_Make(&right, sizeof(Piece)));
        GapBuffer_InsertSlice(&buffer->pieces, targetPieceIdx + 1, Slice_Make(&newPiece, sizeof(Piece)));
    } else {
        size_t insertIdx = (targetPieceIdx < pieceCount && localOffset == 0) ? targetPieceIdx : targetPieceIdx + 1;
        if (insertIdx > GapBuffer_Size(&buffer->pieces)) {
            insertIdx = GapBuffer_Size(&buffer->pieces);
        }
        GapBuffer_InsertSlice(&buffer->pieces, insertIdx, Slice_Make(&newPiece, sizeof(Piece)));
    }

    buffer->totalBytes += len;
    Buffer_InvalidateLineCache(buffer, pos, pos + len, buffer->totalBytes - len);

    if (GapBuffer_Size(&buffer->pieces) > 1000) {
        Buffer_Defragment(buffer);
    }
}

void Buffer_DeleteRange(Buffer* buffer, size_t start, size_t end)
{
    if (start >= end || start >= buffer->totalBytes)
        return;
    if (end > buffer->totalBytes)
        end = buffer->totalBytes;
    size_t deleteLen = end - start;

    LOG_INFO("Buffer_DeleteRange: deleting range [%zu, %zu) (len=%zu)", start, end, deleteLen);

    size_t currentOffset = 0;
    size_t pieceCount = GapBuffer_Size(&buffer->pieces);

    GapBuffer newPieces;
    GapBuffer_Init(&newPieces, sizeof(Piece), pieceCount, alignof(Piece));

    for (size_t i = 0; i < pieceCount; i++) {
        Piece* p = Buffer_GetPiece(buffer, i);
        size_t pieceStart = currentOffset;
        size_t pieceEnd = currentOffset + p->length;

        if (pieceEnd <= start) {
            GapBuffer_InsertSlice(&newPieces, GapBuffer_Size(&newPieces), Slice_Make(p, sizeof(Piece)));
        } else if (pieceStart >= end) {
            GapBuffer_InsertSlice(&newPieces, GapBuffer_Size(&newPieces), Slice_Make(p, sizeof(Piece)));
        } else {
            if (pieceStart < start) {
                Piece left = { .source = p->source, .start = p->start, .length = start - pieceStart };
                GapBuffer_InsertSlice(&newPieces, GapBuffer_Size(&newPieces), Slice_Make(&left, sizeof(Piece)));
            }
            if (pieceEnd > end) {
                size_t skip = end - pieceStart;
                Piece right = { .source = p->source, .start = p->start + skip, .length = pieceEnd - end };
                GapBuffer_InsertSlice(&newPieces, GapBuffer_Size(&newPieces), Slice_Make(&right, sizeof(Piece)));
            }
        }
        currentOffset += p->length;
    }

    GapBuffer_Free(&buffer->pieces);
    buffer->pieces = newPieces;
    buffer->totalBytes -= deleteLen;

    Buffer_InvalidateLineCache(buffer, start, start, buffer->totalBytes + deleteLen);

    if (GapBuffer_Size(&buffer->pieces) > 1000) {
        Buffer_Defragment(buffer);
    }
}

size_t Buffer_GetLineCount(const Buffer* buffer)
{
    assert(buffer);
    Buffer* mutBuffer = (Buffer*)buffer;
    if (mutBuffer->lineCache.dirtyLineStart != SIZE_MAX) {
        Buffer_RebuildLineCache(mutBuffer, SIZE_MAX);
    }
    return GapBuffer_Size(&mutBuffer->lineCache.lines);
}

size_t Buffer_PeekDirtyLineStart(const Buffer* buffer)
{
    assert(buffer);
    return buffer->lineCache.dirtyLineStart;
}

bool Buffer_GetLastRebuildRange(const Buffer* buffer, size_t* oldStart, size_t* oldEnd, size_t* newEnd)
{
    assert(buffer);
    if (!buffer->lastRebuildOccurred) {
        return false;
    }
    *oldStart = buffer->lastRebuiltOldStart;
    *oldEnd = buffer->lastRebuiltOldEnd;
    *newEnd = buffer->lastRebuiltNewEnd;
    return true;
}

size_t Buffer_GetTotalBytes(const Buffer* buffer)
{
    assert(buffer);
    return buffer->totalBytes;
}

Slice Buffer_ToSlice(const Buffer* buffer)
{
    assert(buffer);
    if (buffer->totalBytes == 0) {
        return (Slice) { .data = NULL, .size = 0 };
    }

    char* content = malloc(buffer->totalBytes);
    assert(content);

    size_t offset = 0;
    size_t pieceCount = GapBuffer_Size(&buffer->pieces);
    for (size_t i = 0; i < pieceCount; i++) {
        Piece* p = (Piece*)GapBuffer_At(&buffer->pieces, i);
        const char* src
            = (p->source == PIECE_SOURCE_ORIGINAL) ? buffer->bufferOriginal.data : (const char*)buffer->bufferAdd.data;
        memcpy(content + offset, src + p->start, p->length);
        offset += p->length;
    }

    return (Slice) { .data = content, .size = buffer->totalBytes };
}

bool Buffer_WriteToFileStreaming(const Buffer* buffer, const char* path)
{
    assert(buffer);

    char tempPath[PATH_MAX];
    int fileDescriptor = FileIoOpenTempForAtomicWrite(path, tempPath, sizeof(tempPath));
    if (fileDescriptor == -1)
        return false;

    size_t pieceCount = GapBuffer_Size(&buffer->pieces);
    for (size_t i = 0; i < pieceCount; i++) {
        Piece* p = (Piece*)GapBuffer_At(&buffer->pieces, i);
        const char* src
            = (p->source == PIECE_SOURCE_ORIGINAL) ? buffer->bufferOriginal.data : (const char*)buffer->bufferAdd.data;
        if (!FileIoWriteChunk(fileDescriptor, src + p->start, p->length)) {
            LOG_ERROR("Buffer_WriteToFileStreaming: failed writing piece %zu of %zu to '%s'", i, pieceCount, tempPath);
            FileIoAbortAtomicWrite(fileDescriptor, tempPath);
            return false;
        }
    }

    return FileIoCommitAtomicWrite(fileDescriptor, tempPath, path);
}

// Computes where the cursor should land right before (afterAction=false) or
// right after (afterAction=true) a single action's effect, so undo/redo can
// place the cursor at the edit they just reverted/replayed instead of merely
// clamping whatever the cursor already happened to be.
static void Action_GetCursorPosition(const Action* action, bool afterAction, size_t* outLine, size_t* outColumn)
{
    *outLine = action->lineNumber;
    switch (action->type) {
    case ACTION_INSERT_CHAR:
        *outColumn = afterAction ? action->column + 1 : action->column;
        break;
    case ACTION_DELETE_CHAR:
        *outColumn = afterAction ? action->column : action->column + 1;
        break;
    case ACTION_INSERT_TEXT:
        *outColumn = afterAction ? action->column + action->payload.text.size : action->column;
        break;
    case ACTION_DELETE_TEXT:
        *outColumn = afterAction ? action->column : action->column + action->payload.text.size;
        break;
    case ACTION_SPLIT_LINE:
        if (afterAction) {
            *outLine = action->lineNumber + 1;
            *outColumn = 0;
        } else {
            *outColumn = action->column;
        }
        break;
    case ACTION_JOIN_LINE:
        if (afterAction) {
            *outColumn = action->column;
        } else {
            *outLine = action->lineNumber + 1;
            *outColumn = 0;
        }
        break;
    }
}

bool Buffer_Undo(Buffer* buffer, size_t* outLineNumber, size_t* outColumn)
{
    if (!buffer)
        return false;
    LOG_INFO("Buffer_Undo: executing undo.");
    buffer->history.currentGroup = NULL;
    ActionGroup* group = (ActionGroup*)Stack_Pop(&buffer->history.undoStack);
    if (!group) {
        LOG_INFO("Buffer_Undo: undo stack is empty.");
        return false;
    }

    Action* firstAction = Array_Size(&group->actions) > 0 ? (Action*)Array_At(&group->actions, 0) : NULL;
    size_t rangeStartOffset = SIZE_MAX;
    if (firstAction) {
        Line* line = Buffer_GetLine(buffer, firstAction->lineNumber);
        if (line) {
            rangeStartOffset = line->offset;
        }
    }

    buffer->history.isUndoRedoing = true;

    // A group can be undone more than once (undo -> redo -> undo), so drop
    // any snapshotAfter from a previous undo of this same group before
    // replacing it.
    DocumentSnapshot_Free(group->snapshotAfter);
    group->snapshotAfter = DocumentSnapshot_Copy(buffer);
    Buffer_RestoreSnapshot(buffer, group->snapshotBefore, rangeStartOffset);

    buffer->history.isUndoRedoing = false;

    if (firstAction) {
        Action_GetCursorPosition(firstAction, false, outLineNumber, outColumn);
    }

    Stack_Push(&buffer->history.redoStack, group);
    Stack_EnforceLimit(&buffer->history.redoStack, buffer->history.undoLimit, (void (*)(void*))ActionGroup_Free);
    return true;
}

bool Buffer_Redo(Buffer* buffer, size_t* outLineNumber, size_t* outColumn)
{
    if (!buffer)
        return false;
    LOG_INFO("Buffer_Redo: executing redo.");
    buffer->history.currentGroup = NULL;
    ActionGroup* group = (ActionGroup*)Stack_Pop(&buffer->history.redoStack);
    if (!group) {
        LOG_INFO("Buffer_Redo: redo stack is empty.");
        return false;
    }

    size_t actionCount = Array_Size(&group->actions);
    Action* firstAction = actionCount > 0 ? (Action*)Array_At(&group->actions, 0) : NULL;
    size_t rangeStartOffset = SIZE_MAX;
    if (firstAction) {
        Line* line = Buffer_GetLine(buffer, firstAction->lineNumber);
        if (line) {
            rangeStartOffset = line->offset;
        }
    }

    buffer->history.isUndoRedoing = true;

    Buffer_RestoreSnapshot(buffer, group->snapshotAfter, rangeStartOffset);

    buffer->history.isUndoRedoing = false;

    if (actionCount > 0) {
        Action* lastAction = (Action*)Array_At(&group->actions, actionCount - 1);
        Action_GetCursorPosition(lastAction, true, outLineNumber, outColumn);
    }

    Stack_Push(&buffer->history.undoStack, group);
    Stack_EnforceLimit(&buffer->history.undoStack, buffer->history.undoLimit, (void (*)(void*))ActionGroup_Free);
    return true;
}

void Buffer_OnSave(Buffer* buffer, const char* path)
{
    LOG_INFO("Buffer_OnSave: saving buffer to '%s' (size=%zu bytes)", path, buffer->totalBytes);
    if (!Buffer_WriteToFileStreaming(buffer, path)) {
        LOG_ERROR("Buffer_OnSave: failed to write file to '%s'", path);
        return;
    }

    MappedFile_Unmap(&buffer->mappedFile);

    MappedFile newMmap = FileIoMmap(path);
    if (newMmap.fileDescriptor != -1) {
        buffer->mappedFile = newMmap;
        buffer->bufferOriginal = newMmap.content;
    } else {
        buffer->bufferOriginal = (Slice) { .data = NULL, .size = 0 };
    }

    GapBuffer_Clear(&buffer->pieces);
    if (buffer->bufferOriginal.size > 0) {
        Piece p = { .source = PIECE_SOURCE_ORIGINAL, .start = 0, .length = buffer->bufferOriginal.size };
        GapBuffer_InsertSlice(&buffer->pieces, 0, Slice_Make(&p, sizeof(Piece)));
    }

    Array_Clear(&buffer->bufferAdd);
    buffer->isModified = false;
}
