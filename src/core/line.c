#include "line.h"
#include "buffer.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

// Raw-byte spacing between render checkpoints. Smaller = faster queries, more memory.
#define RENDER_CHECKPOINT_STRIDE 4096

size_t Line_Length(Line* line)
{
    assert(line != NULL && "Line cannot be NULL");
    return line->length;
}

Slice Line_GetTextRange(Line* line, struct Buffer* buffer, size_t start, size_t length)
{
    assert(line != NULL && "Line cannot be NULL");
    assert(buffer != NULL && "Buffer cannot be NULL");

    size_t lineLength = line->length;
    if (start >= lineLength || length == 0) {
        return (Slice) { .data = "", .size = 0 };
    }
    size_t available = lineLength - start;
    if (length > available) {
        length = available;
    }

    // Reused, growable scratch buffer -- avoids a malloc/cache of the line's
    // entire text just to serve a small sub-range (e.g. a visible viewport
    // slice of a huge line). Sized length+1 and NUL-terminated as defense in
    // depth: consumers receive an exact-length Slice, but a stray C-string
    // read one past the end must never leave the allocation or see a stale
    // byte from a previous, longer fetch.
    static char* scratch = NULL;
    static size_t scratchCapacity = 0;
    if (length >= scratchCapacity) {
        char* newScratch = realloc(scratch, length + 1);
        if (!newScratch) {
            // Out of memory: an empty slice is the only safe answer -- the
            // old scratch (if any) stays valid for future smaller requests.
            return (Slice) { .data = scratch, .size = 0 };
        }
        scratch = newScratch;
        scratchCapacity = length + 1;
    }
    scratch[length] = '\0';

    size_t rangeStart = line->offset + start;
    size_t rangeEnd = rangeStart + length;

    size_t currentOffset = 0;
    size_t pieceCount = GapBuffer_Size(&buffer->pieces);
    size_t destOffset = 0;

    for (size_t i = 0; i < pieceCount && destOffset < length; i++) {
        Piece* p = (Piece*)GapBuffer_At(&buffer->pieces, i);
        size_t pieceStart = currentOffset;
        size_t pieceEnd = currentOffset + p->length;

        if (pieceEnd > rangeStart && pieceStart < rangeEnd) {
            size_t overlapStart = (rangeStart > pieceStart) ? rangeStart : pieceStart;
            size_t overlapEnd = (rangeEnd < pieceEnd) ? rangeEnd : pieceEnd;
            size_t overlapLen = overlapEnd - overlapStart;

            size_t localOffset = overlapStart - pieceStart;
            const char* src = Buffer_PieceData(buffer, p);

            memcpy(scratch + destOffset, src + p->start + localOffset, overlapLen);
            destOffset += overlapLen;
        }
        currentOffset += p->length;
    }
    assert(destOffset == length);

    return Slice_Make(scratch, length);
}

Slice Line_GetText(Line* line, struct Buffer* buffer)
{
    assert(line != NULL && "Line cannot be NULL");
    return Line_GetTextRange(line, buffer, 0, line->length);
}

char Line_GetChar(Line* line, struct Buffer* buffer, size_t index)
{
    assert(line != NULL && "Line cannot be NULL");
    assert(index < line->length && "Index out of bounds");
    Slice textSlice = Line_GetTextRange(line, buffer, index, 1);
    return ((const char*)textSlice.data)[0];
}

// Builds (or rebuilds, if `tabSize` differs from what's cached) the
// render-column checkpoint index for a huge line, reading it in bounded
// chunks via Line_GetTextRange rather than materializing/caching the line's
// entire text just to compute this once.
static void Line_EnsureRenderCheckpoints(Line* line, struct Buffer* buffer, size_t tabSize)
{
    if (line->renderCheckpoints && line->renderCheckpointTabSize == tabSize) {
        return;
    }
    if (!line->renderCheckpoints) {
        line->renderCheckpoints = calloc(1, sizeof(Array));
        assert(line->renderCheckpoints != NULL);
        Array_InitStruct(line->renderCheckpoints, RenderCheckpoint, 4);
    } else {
        Array_Clear(line->renderCheckpoints);
    }

    size_t length = line->length;
    size_t pos = 0;
    size_t rx = 0;
    size_t nextCheckpointAt = 0;
    while (pos < length) {
        size_t chunkLen = length - pos < RENDER_CHECKPOINT_STRIDE ? length - pos : RENDER_CHECKPOINT_STRIDE;
        Slice chunk = Line_GetTextRange(line, buffer, pos, chunkLen);
        const char* chunkData = (const char*)chunk.data;
        for (size_t i = 0; i < chunk.size; i++) {
            if (pos + i == nextCheckpointAt) {
                RenderCheckpoint cp = { .rawCol = pos + i, .renderCol = rx };
                Array_Append(line->renderCheckpoints, &cp, 1);
                nextCheckpointAt += RENDER_CHECKPOINT_STRIDE;
            }
            if (chunkData[i] == '\t')
                rx += (tabSize - 1) - (rx % tabSize);
            rx++;
        }
        pos += chunk.size;
    }
    line->renderCheckpointTabSize = tabSize;
}

size_t Line_GetRenderX(Line* line, struct Buffer* buffer, size_t cursorX, size_t tabSize)
{
    assert(line != NULL && "Line cannot be NULL");

    size_t length = line->length;
    if (length <= LINE_HUGE_THRESHOLD) {
        Slice text = Line_GetText(line, buffer);
        size_t rx = 0;
        for (size_t i = 0; i < cursorX && i < text.size; i++) {
            assert(text.data != NULL && "Text data should not be NULL");
            if (((const char*)text.data)[i] == '\t')
                rx += (tabSize - 1) - (rx % tabSize);
            rx++;
        }
        return rx;
    }

    Line_EnsureRenderCheckpoints(line, buffer, tabSize);

    size_t targetCol = cursorX < length ? cursorX : length;

    // Binary search for the last checkpoint at or before targetCol.
    size_t low = 0, high = Array_Size(line->renderCheckpoints);
    while (low < high) {
        size_t mid = low + (high - low) / 2;
        RenderCheckpoint* cp = (RenderCheckpoint*)Array_At(line->renderCheckpoints, mid);
        if (cp->rawCol <= targetCol) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }

    size_t startCol = 0;
    size_t rx = 0;
    if (low > 0) {
        RenderCheckpoint* cp = (RenderCheckpoint*)Array_At(line->renderCheckpoints, low - 1);
        startCol = cp->rawCol;
        rx = cp->renderCol;
    }

    Slice tail = Line_GetTextRange(line, buffer, startCol, targetCol - startCol);
    const char* tailData = (const char*)tail.data;
    for (size_t i = 0; i < tail.size; i++) {
        if (tailData[i] == '\t')
            rx += (tabSize - 1) - (rx % tabSize);
        rx++;
    }
    return rx;
}

void Line_Free(Line* line)
{
    if (!line)
        return;
    if (line->styles) {
        Array_Free(line->styles);
        free(line->styles);
        line->styles = NULL;
    }
    if (line->renderCheckpoints) {
        Array_Free(line->renderCheckpoints);
        free(line->renderCheckpoints);
        line->renderCheckpoints = NULL;
    }
}
