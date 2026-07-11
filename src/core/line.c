#include "../neo.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

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
        char dummy = 0;
        Array_Append(&line->testText, &dummy, 1);
        memmove((char*)line->testText.data + position + 1, (char*)line->testText.data + position,
            line->testText.size - 1 - position);
        ((char*)line->testText.data)[position] = character;

        // Sync styles size
        Array_Append(&line->styles, &dummy, 1);
        memmove((char*)line->styles.data + position + 1, (char*)line->styles.data + position,
            line->styles.size - 1 - position);
        ((char*)line->styles.data)[position] = 0;
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
        memmove((char*)line->testText.data + position, (char*)line->testText.data + position + 1,
            line->testText.size - 1 - position);
        line->testText.size--;

        memmove((char*)line->styles.data + position, (char*)line->styles.data + position + 1,
            line->styles.size - 1 - position);
        line->styles.size--;
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
        size_t oldSize = line->testText.size;
        for (size_t i = 0; i < length; i++) {
            char dummy = 0;
            Array_Append(&line->testText, &dummy, 1);
            Array_Append(&line->styles, &dummy, 1);
        }
        memmove(
            (char*)line->testText.data + position + length, (char*)line->testText.data + position, oldSize - position);
        memcpy((char*)line->testText.data + position, text, length);

        memmove((char*)line->styles.data + position + length, (char*)line->styles.data + position, oldSize - position);
        memset((char*)line->styles.data + position, 0, length);
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
        memmove((char*)line->testText.data + position, (char*)line->testText.data + position + length,
            line->testText.size - position - length);
        line->testText.size -= length;

        memmove((char*)line->styles.data + position, (char*)line->styles.data + position + length,
            line->styles.size - position - length);
        line->styles.size -= length;
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

size_t Line_GetRenderX(Line* line, size_t cursorX, size_t tabSize)
{
    assert(line != NULL && "Line cannot be NULL");
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
