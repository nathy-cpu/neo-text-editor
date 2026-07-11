#include "../neo.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// LINE CACHE INTERNAL METHODS
// ============================================================================

static void LineCache_Init(LineCache* lc)
{
    lc->lines = malloc(sizeof(Line) * 64);
    assert(lc->lines);
    lc->count = 0;
    lc->gapStart = 0;
    lc->gapEnd = 64;
    lc->capacity = 64;
    lc->dirtyLineStart = 0;
}

static void LineCache_Free(LineCache* lc)
{
    if (!lc->lines)
        return;
    for (size_t i = 0; i < lc->capacity; i++) {
        if (i < lc->gapStart || i >= lc->gapEnd) {
            free(lc->lines[i].text);
            Array_Free(&lc->lines[i].styles);
            Array_Free(&lc->lines[i].testText);
        }
    }
    free(lc->lines);
    lc->lines = NULL;
    lc->count = lc->gapStart = lc->gapEnd = lc->capacity = 0;
}

static void LineCache_MoveGap(LineCache* lc, size_t newGapStart)
{
    if (newGapStart == lc->gapStart)
        return;

    size_t gapSize = lc->gapEnd - lc->gapStart;
    if (newGapStart > lc->gapStart) {
        size_t moveCount = newGapStart - lc->gapStart;
        memmove(&lc->lines[lc->gapStart], &lc->lines[lc->gapEnd], sizeof(Line) * moveCount);
    } else {
        size_t moveCount = lc->gapStart - newGapStart;
        memmove(&lc->lines[newGapStart + gapSize], &lc->lines[newGapStart], sizeof(Line) * moveCount);
    }
    lc->gapStart = newGapStart;
    lc->gapEnd = newGapStart + gapSize;
}

static void LineCache_EnsureCapacity(LineCache* lc, size_t required)
{
    size_t gapSize = lc->gapEnd - lc->gapStart;
    if (gapSize >= required)
        return;

    size_t newCapacity = lc->capacity * 2 + required;
    size_t newGapSize = newCapacity - lc->count;
    Line* newLines = malloc(sizeof(Line) * newCapacity);
    assert(newLines);

    // Copy elements before gap
    memcpy(newLines, lc->lines, sizeof(Line) * lc->gapStart);
    // Copy elements after gap
    size_t afterCount = lc->capacity - lc->gapEnd;
    memcpy(&newLines[lc->gapStart + newGapSize], &lc->lines[lc->gapEnd], sizeof(Line) * afterCount);

    free(lc->lines);
    lc->lines = newLines;
    lc->gapEnd = lc->gapStart + newGapSize;
    lc->capacity = newCapacity;
}

static void LineCache_Insert(LineCache* lc, Line* line)
{
    LineCache_EnsureCapacity(lc, 1);
    lc->lines[lc->gapStart] = *line;
    lc->gapStart++;
    lc->count++;
}

static Line* LineCache_At(const LineCache* lc, size_t index)
{
    assert(index < lc->count);
    if (index < lc->gapStart) {
        return &lc->lines[index];
    } else {
        return &lc->lines[index + (lc->gapEnd - lc->gapStart)];
    }
}

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

static void Buffer_RebuildLineCache(Buffer* buffer, size_t upToLineIndex)
{
    if (buffer->lineCache.dirtyLineStart == SIZE_MAX) {
        return;
    }
    if (buffer->lineCache.dirtyLineStart > upToLineIndex && upToLineIndex != SIZE_MAX) {
        return;
    }

    size_t lineIdx = buffer->lineCache.dirtyLineStart;

    // Save old lines for preserving state
    size_t oldTotal = buffer->lineCache.count;
    size_t numOldLinesToSave = (oldTotal > lineIdx) ? (oldTotal - lineIdx) : 0;
    Line* savedLines = NULL;
    if (numOldLinesToSave > 0) {
        savedLines = malloc(sizeof(Line) * numOldLinesToSave);
        assert(savedLines);
        for (size_t i = 0; i < numOldLinesToSave; i++) {
            Line* oldLine = LineCache_At(&buffer->lineCache, lineIdx + i);
            savedLines[i] = *oldLine;
            // Clear style/text pointers so they don't get double-freed
            oldLine->text = NULL;
            oldLine->styles = (Array) { 0 };
            oldLine->testText = (Array) { 0 };
        }
    }

    // Truncate the lineCache to lineIdx
    LineCache_MoveGap(&buffer->lineCache, lineIdx);

    // Free elements after gap
    for (size_t i = buffer->lineCache.gapEnd; i < buffer->lineCache.capacity; i++) {
        free(buffer->lineCache.lines[i].text);
        buffer->lineCache.lines[i].text = NULL;
        Array_Free(&buffer->lineCache.lines[i].styles);
        Array_Free(&buffer->lineCache.lines[i].testText);
    }
    buffer->lineCache.gapEnd = buffer->lineCache.capacity;
    buffer->lineCache.count = lineIdx;

    // Find starting offset for scanning
    size_t currentOffset = 0;
    if (lineIdx > 0) {
        Line* prevLine = LineCache_At(&buffer->lineCache, lineIdx - 1);
        currentOffset = prevLine->offset + prevLine->length + 1; // +1 for the newline
    }

    PiecePosition pos = Buffer_FindPiecePosition(buffer, currentOffset);
    size_t pieceIdx = pos.pieceIndex;
    size_t localOffset = pos.localOffset;
    size_t pieceCount = GapBuffer_Size(&buffer->pieces);

    size_t scanOffset = currentOffset;
    size_t lineStartOffset = currentOffset;

    while (pieceIdx < pieceCount) {
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
                    .lineNumber = lineIdx,
                    .isFolded = false,
                    .foldLevel = 0,
                    .styles = { 0 },
                    .testText = { 0 } };
                LineCache_Insert(&buffer->lineCache, &newLine);
                lineIdx++;
                scanOffset += (foundIdx - j) + 1;
                lineStartOffset = scanOffset;
                j = foundIdx + 1;
            } else {
                scanOffset += (p->length - j);
                break;
            }
        }
        pieceIdx++;
        localOffset = 0;
    }

    // Handle last line
    if (lineStartOffset <= buffer->totalBytes) {
        size_t lineLen = buffer->totalBytes - lineStartOffset;
        Line newLine = { .buffer = buffer,
            .offset = lineStartOffset,
            .length = lineLen,
            .text = NULL,
            .lineNumber = lineIdx,
            .isFolded = false,
            .foldLevel = 0,
            .styles = { 0 },
            .testText = { 0 } };
        LineCache_Insert(&buffer->lineCache, &newLine);
        lineIdx++;
    }

    // Copy back preserved folding & style state
    size_t newTotal = buffer->lineCache.count;
    for (size_t i = buffer->lineCache.dirtyLineStart; i < newTotal; i++) {
        size_t oldIdx = i - newTotal + oldTotal;
        if (oldIdx >= buffer->lineCache.dirtyLineStart && oldIdx < oldTotal) {
            size_t savedIdx = oldIdx - buffer->lineCache.dirtyLineStart;
            if (savedIdx < numOldLinesToSave) {
                Line* newLine = LineCache_At(&buffer->lineCache, i);
                newLine->isFolded = savedLines[savedIdx].isFolded;
                newLine->foldLevel = savedLines[savedIdx].foldLevel;
                // Move styles array (shallow copy)
                Array_Free(&newLine->styles);
                newLine->styles = savedLines[savedIdx].styles;
                savedLines[savedIdx].styles.data = NULL; // prevent double-free
            }
        }
    }

    // Free the savedLines memory
    if (savedLines) {
        for (size_t i = 0; i < numOldLinesToSave; i++) {
            free(savedLines[i].text);
            Array_Free(&savedLines[i].styles);
            Array_Free(&savedLines[i].testText);
        }
        free(savedLines);
    }

    buffer->lineCache.dirtyLineStart = SIZE_MAX;
}

void Buffer_InvalidateLineCache(Buffer* buffer, size_t offset)
{
    size_t lineIdx = 0;
    if (offset > 0 && buffer->lineCache.dirtyLineStart != 0) {
        size_t searchLimit = (buffer->lineCache.dirtyLineStart == SIZE_MAX) ? buffer->lineCache.count
                                                                            : buffer->lineCache.dirtyLineStart;
        for (size_t i = 0; i < searchLimit; i++) {
            Line* line = LineCache_At(&buffer->lineCache, i);
            if (line->offset + line->length >= offset) {
                lineIdx = i;
                break;
            }
        }
    }

    if (lineIdx < buffer->lineCache.dirtyLineStart) {
        buffer->lineCache.dirtyLineStart = lineIdx;
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
    LOG_DEBUG("DocumentSnapshot_Copy: creating snapshot of buffer %p (totalBytes=%zu)", (void*)buffer, buffer->totalBytes);
    DocumentSnapshot* snap = malloc(sizeof(DocumentSnapshot));
    assert(snap);

    snap->totalBytes = buffer->totalBytes;
    GapBuffer_Init(&snap->pieces, sizeof(Piece), GapBuffer_Size(&buffer->pieces), alignof(Piece));

    Array_Append(&snap->pieces.data, buffer->pieces.data.data, buffer->pieces.data.size);
    snap->pieces.gapStart = buffer->pieces.gapStart;
    snap->pieces.gapEnd = buffer->pieces.gapEnd;
    snap->pieces.data.size = buffer->pieces.data.size;

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

void Buffer_RestoreSnapshot(Buffer* buffer, DocumentSnapshot* snap)
{
    assert(buffer && snap);
    LOG_DEBUG("Buffer_RestoreSnapshot: restoring snapshot %p to buffer %p (totalBytes=%zu)", (void*)snap, (void*)buffer, snap->totalBytes);

    GapBuffer_Free(&buffer->pieces);

    GapBuffer_Init(&buffer->pieces, sizeof(Piece), GapBuffer_Size(&snap->pieces), alignof(Piece));
    Array_Append(&buffer->pieces.data, snap->pieces.data.data, snap->pieces.data.size);
    buffer->pieces.gapStart = snap->pieces.gapStart;
    buffer->pieces.gapEnd = snap->pieces.gapEnd;
    buffer->pieces.data.size = snap->pieces.data.size;

    buffer->totalBytes = snap->totalBytes;

    Buffer_InvalidateLineCache(buffer, 0);
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
        .testText = { 0 } };
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

    if (lineNumber >= mutBuffer->lineCache.count) {
        return NULL;
    }

    return Buffer_GetCachedLine(mutBuffer, lineNumber);
}

Line* Buffer_InsertLine(Buffer* buffer, size_t lineNumber)
{
    assert(buffer);
    LOG_DEBUG("Buffer_InsertLine: inserting line at index %zu (totalLines=%zu)",
        lineNumber, Buffer_GetLineCount(buffer));
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
    LOG_DEBUG("Buffer_DeleteLine: deleting line at index %zu (totalLines=%zu)",
        lineNumber, Buffer_GetLineCount(buffer));
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
            Buffer_InvalidateLineCache(buffer, pos);
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
                Buffer_InvalidateLineCache(buffer, pos);
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
    Buffer_InvalidateLineCache(buffer, pos);

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

    Buffer_InvalidateLineCache(buffer, start);

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
    return mutBuffer->lineCache.count;
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

void Buffer_Undo(Buffer* buffer)
{
    if (!buffer)
        return;
    LOG_INFO("Buffer_Undo: executing undo.");
    buffer->history.currentGroup = NULL;
    ActionGroup* group = (ActionGroup*)Stack_Pop(&buffer->history.undoStack);
    if (!group) {
        LOG_INFO("Buffer_Undo: undo stack is empty.");
        return;
    }

    buffer->history.isUndoRedoing = true;

    group->snapshotAfter = DocumentSnapshot_Copy(buffer);
    Buffer_RestoreSnapshot(buffer, group->snapshotBefore);

    buffer->history.isUndoRedoing = false;

    Stack_Push(&buffer->history.redoStack, group);
    Stack_EnforceLimit(&buffer->history.redoStack, buffer->history.undoLimit, (void (*)(void*))ActionGroup_Free);
}

void Buffer_Redo(Buffer* buffer)
{
    if (!buffer)
        return;
    LOG_INFO("Buffer_Redo: executing redo.");
    buffer->history.currentGroup = NULL;
    ActionGroup* group = (ActionGroup*)Stack_Pop(&buffer->history.redoStack);
    if (!group) {
        LOG_INFO("Buffer_Redo: redo stack is empty.");
        return;
    }

    buffer->history.isUndoRedoing = true;

    Buffer_RestoreSnapshot(buffer, group->snapshotAfter);

    buffer->history.isUndoRedoing = false;

    Stack_Push(&buffer->history.undoStack, group);
    Stack_EnforceLimit(&buffer->history.undoStack, buffer->history.undoLimit, (void (*)(void*))ActionGroup_Free);
}

void Buffer_OnSave(Buffer* buffer, const char* path)
{
    Slice content = Buffer_ToSlice(buffer);
    LOG_INFO("Buffer_OnSave: saving buffer to '%s' (size=%zu bytes)", path, content.size);
    if (!FileIoWrite(path, content)) {
        LOG_ERROR("Buffer_OnSave: failed to write file to '%s'", path);
        free((void*)content.data);
        return;
    }
    free((void*)content.data);

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
    buffer->lineCache.dirtyLineStart = 0;
    buffer->isModified = false;
}
