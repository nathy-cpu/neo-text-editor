#include "../neo.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

// Raw-byte spacing between render checkpoints. Smaller = faster queries, more memory.
#define RENDER_CHECKPOINT_STRIDE 4096

Line* Line_New(size_t initialCapacity)
{
    assert(initialCapacity > 0 && "Initial capacity must be greater than 0");
    Line* line = calloc(1, sizeof(Line));
    assert(line != NULL && "Memory allocation failed for Line_New");

    Array_Init(&line->testText, sizeof(char), initialCapacity, 0);
    Array_Init(&line->styles, sizeof(char), initialCapacity, 0);

    line->buffer = NULL;
    line->offset = 0;
    line->length = 0;
    line->text = NULL;
    line->lineNumber = 0;
    line->isFolded = false;
    line->foldLevel = 0;
    line->renderCheckpoints = NULL;
    line->renderCheckpointCount = 0;
    line->renderCheckpointTabSize = 0;

    LOG_DEBUG("Line_New: allocated line %p with capacity %zu", (void*)line, initialCapacity);
    return line;
}

void Line_Free(Line* line)
{
    if (!line)
        return;

    LOG_DEBUG("Line_Free: freeing line %p (lineNumber=%zu, offset=%zu, length=%zu)",
        (void*)line, line->lineNumber, line->offset, line->length);
    free(line->text);
    Array_Free(&line->testText);
    Array_Free(&line->styles);
    free(line->renderCheckpoints);
    free(line);
}

void Line_InsertChar(Line* line, size_t position, char character)
{
    assert(line != NULL && "Line cannot be NULL");
    LOG_DEBUG("Line_InsertChar: inserting '%c' at position %zu for line %zu (buffer=%p)",
        character, position, line->lineNumber, (void*)line->buffer);
    if (line->buffer != NULL) {
        Buffer_InsertChar(line->buffer, line->lineNumber, position, character);
    } else {
        if (position > line->testText.size)
            position = line->testText.size;
        Array_ReplaceRange(&line->testText, position, 0, &character, 1);
        Array_ReplaceRange(&line->styles, position, 0, NULL, 1);
        *(char*)Array_RawAt(&line->styles, position) = 0;
    }
}

void Line_DeleteChar(Line* line, size_t position)
{
    assert(line != NULL && "Line cannot be NULL");
    LOG_DEBUG("Line_DeleteChar: deleting char at position %zu for line %zu (buffer=%p)",
        position, line->lineNumber, (void*)line->buffer);
    if (line->buffer != NULL) {
        Buffer_DeleteChar(line->buffer, line->lineNumber, position);
    } else {
        assert(position < line->testText.size && "Delete position out of bounds");
        Array_ReplaceRange(&line->testText, position, 1, NULL, 0);
        Array_ReplaceRange(&line->styles, position, 1, NULL, 0);
    }
}

void Line_InsertText(Line* line, size_t position, const char* text, size_t length)
{
    assert(line != NULL && "Line cannot be NULL");
    assert(text != NULL && "Text pointer cannot be NULL");
    LOG_DEBUG("Line_InsertText: inserting text of length %zu at position %zu for line %zu (buffer=%p)",
        length, position, line->lineNumber, (void*)line->buffer);
    if (line->buffer != NULL) {
        size_t offset = line->offset + position;
        Buffer_InsertText(line->buffer, offset, text, length);
    } else {
        if (length == 0)
            return;
        if (position > line->testText.size)
            position = line->testText.size;
        Array_ReplaceRange(&line->testText, position, 0, text, length);
        Array_ReplaceRange(&line->styles, position, 0, NULL, length);
        memset(Array_RawAt(&line->styles, position), 0, length);
    }
}

void Line_DeleteText(Line* line, size_t position, size_t length)
{
    assert(line != NULL && "Line cannot be NULL");
    LOG_DEBUG("Line_DeleteText: deleting %zu chars starting at position %zu for line %zu (buffer=%p)",
        length, position, line->lineNumber, (void*)line->buffer);
    if (line->buffer != NULL) {
        size_t offset = line->offset + position;
        Buffer_DeleteRange(line->buffer, offset, offset + length);
    } else {
        if (length == 0)
            return;
        assert(position + length <= line->testText.size && "Delete range out of bounds");
        Array_ReplaceRange(&line->testText, position, length, NULL, 0);
        Array_ReplaceRange(&line->styles, position, length, NULL, 0);
    }
}

size_t Line_Length(Line* line)
{
    assert(line != NULL && "Line cannot be NULL");
    if (line->buffer != NULL) {
        return line->length;
    } else {
        return line->testText.size;
    }
}

Slice Line_GetText(Line* line)
{
    assert(line != NULL && "Line cannot be NULL");
    if (line->buffer != NULL) {
        if (line->length == 0) {
            return (Slice) { .data = "", .size = 0 };
        }
        if (!line->text) {
            line->text = malloc(line->length + 1);
            assert(line->text != NULL);

            size_t currentOffset = 0;
            size_t pieceCount = GapBuffer_Size(&line->buffer->pieces);
            size_t bytesToCopy = line->length;
            size_t destOffset = 0;

            for (size_t i = 0; i < pieceCount && bytesToCopy > 0; i++) {
                Piece* p = (Piece*)GapBuffer_At(&line->buffer->pieces, i);
                size_t pieceStart = currentOffset;
                size_t pieceEnd = currentOffset + p->length;

                if (pieceEnd > line->offset && pieceStart < line->offset + line->length) {
                    size_t overlapStart = (line->offset > pieceStart) ? line->offset : pieceStart;
                    size_t overlapEnd
                        = (line->offset + line->length < pieceEnd) ? (line->offset + line->length) : pieceEnd;
                    size_t overlapLen = overlapEnd - overlapStart;

                    size_t localOffset = overlapStart - pieceStart;
                    const char* src = (p->source == PIECE_SOURCE_ORIGINAL) ? line->buffer->bufferOriginal.data
                                                                           : (const char*)line->buffer->bufferAdd.data;

                    memcpy(line->text + destOffset, src + p->start + localOffset, overlapLen);
                    destOffset += overlapLen;
                    bytesToCopy -= overlapLen;
                }
                currentOffset += p->length;
            }
            assert(destOffset == line->length);
            line->text[line->length] = '\0';
        }
        return Slice_Make(line->text, line->length);
    } else {
        return (Slice) { .data = line->testText.data, .size = line->testText.size };
    }
}

Slice Line_GetTextRange(Line* line, size_t start, size_t length)
{
    assert(line != NULL && "Line cannot be NULL");

    size_t lineLength = Line_Length(line);
    if (start >= lineLength || length == 0) {
        return (Slice) { .data = "", .size = 0 };
    }
    size_t available = lineLength - start;
    if (length > available) {
        length = available;
    }

    if (line->buffer == NULL) {
        return Slice_Make((const char*)line->testText.data + start, length);
    }

    // Reused, growable scratch buffer -- avoids a malloc/cache of the line's
    // entire text just to serve a small sub-range (e.g. a visible viewport
    // slice of a huge line).
    static char* scratch = NULL;
    static size_t scratchCapacity = 0;
    if (length > scratchCapacity) {
        char* newScratch = realloc(scratch, length);
        assert(newScratch != NULL);
        scratch = newScratch;
        scratchCapacity = length;
    }

    size_t rangeStart = line->offset + start;
    size_t rangeEnd = rangeStart + length;

    size_t currentOffset = 0;
    size_t pieceCount = GapBuffer_Size(&line->buffer->pieces);
    size_t destOffset = 0;

    for (size_t i = 0; i < pieceCount && destOffset < length; i++) {
        Piece* p = (Piece*)GapBuffer_At(&line->buffer->pieces, i);
        size_t pieceStart = currentOffset;
        size_t pieceEnd = currentOffset + p->length;

        if (pieceEnd > rangeStart && pieceStart < rangeEnd) {
            size_t overlapStart = (rangeStart > pieceStart) ? rangeStart : pieceStart;
            size_t overlapEnd = (rangeEnd < pieceEnd) ? rangeEnd : pieceEnd;
            size_t overlapLen = overlapEnd - overlapStart;

            size_t localOffset = overlapStart - pieceStart;
            const char* src = (p->source == PIECE_SOURCE_ORIGINAL) ? line->buffer->bufferOriginal.data
                                                                    : (const char*)line->buffer->bufferAdd.data;

            memcpy(scratch + destOffset, src + p->start + localOffset, overlapLen);
            destOffset += overlapLen;
        }
        currentOffset += p->length;
    }
    assert(destOffset == length);

    return Slice_Make(scratch, length);
}

char Line_GetChar(Line* line, size_t index)
{
    assert(line != NULL && "Line cannot be NULL");
    if (line->buffer != NULL) {
        assert(index < line->length && "Index out of bounds");
        Slice textSlice = Line_GetText(line);
        return ((const char*)textSlice.data)[index];
    } else {
        assert(index < line->testText.size && "Index out of bounds");
        return ((const char*)line->testText.data)[index];
    }
}

// Builds (or rebuilds, if `tabSize` differs from what's cached) the
// render-column checkpoint index for a huge line, reading it in bounded
// chunks via Line_GetTextRange rather than materializing/caching the line's
// entire text just to compute this once.
static void Line_EnsureRenderCheckpoints(Line* line, size_t tabSize)
{
    if (line->renderCheckpoints && line->renderCheckpointTabSize == tabSize) {
        return;
    }
    free(line->renderCheckpoints);
    line->renderCheckpoints = NULL;
    line->renderCheckpointCount = 0;

    size_t length = Line_Length(line);
    size_t capacity = length / RENDER_CHECKPOINT_STRIDE + 1;
    RenderCheckpoint* checkpoints = malloc(sizeof(RenderCheckpoint) * capacity);
    assert(checkpoints != NULL);

    size_t count = 0;
    size_t rx = 0;
    size_t nextCheckpointAt = 0;
    size_t pos = 0;
    while (pos < length) {
        size_t chunkLen = length - pos < RENDER_CHECKPOINT_STRIDE ? length - pos : RENDER_CHECKPOINT_STRIDE;
        Slice chunk = Line_GetTextRange(line, pos, chunkLen);
        const char* chunkData = (const char*)chunk.data;
        for (size_t i = 0; i < chunk.size; i++) {
            if (pos + i == nextCheckpointAt) {
                assert(count < capacity);
                checkpoints[count].rawCol = pos + i;
                checkpoints[count].renderCol = rx;
                count++;
                nextCheckpointAt += RENDER_CHECKPOINT_STRIDE;
            }
            if (chunkData[i] == '\t')
                rx += (tabSize - 1) - (rx % tabSize);
            rx++;
        }
        pos += chunk.size;
    }

    line->renderCheckpoints = checkpoints;
    line->renderCheckpointCount = count;
    line->renderCheckpointTabSize = tabSize;
}

size_t Line_GetRenderX(Line* line, size_t cursorX, size_t tabSize)
{
    assert(line != NULL && "Line cannot be NULL");

    size_t length = Line_Length(line);
    if (length <= LINE_HUGE_THRESHOLD) {
        Slice text = Line_GetText(line);
        size_t rx = 0;
        for (size_t i = 0; i < cursorX && i < text.size; i++) {
            assert(text.data != NULL && "Text data should not be NULL");
            if (((const char*)text.data)[i] == '\t')
                rx += (tabSize - 1) - (rx % tabSize);
            rx++;
        }
        return rx;
    }

    Line_EnsureRenderCheckpoints(line, tabSize);

    size_t targetCol = cursorX < length ? cursorX : length;

    // Binary search for the last checkpoint at or before targetCol.
    size_t low = 0, high = line->renderCheckpointCount;
    while (low < high) {
        size_t mid = low + (high - low) / 2;
        if (line->renderCheckpoints[mid].rawCol <= targetCol) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }

    size_t startCol = 0;
    size_t rx = 0;
    if (low > 0) {
        startCol = line->renderCheckpoints[low - 1].rawCol;
        rx = line->renderCheckpoints[low - 1].renderCol;
    }

    Slice tail = Line_GetTextRange(line, startCol, targetCol - startCol);
    const char* tailData = (const char*)tail.data;
    for (size_t i = 0; i < tail.size; i++) {
        if (tailData[i] == '\t')
            rx += (tabSize - 1) - (rx % tabSize);
        rx++;
    }
    return rx;
}
