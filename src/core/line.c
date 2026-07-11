#include "../neo.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

Line* Line_New(size_t initialCapacity)
{
    assert(initialCapacity > 0 && "Initial capacity must be greater than 0");
    Line* line = malloc(sizeof(Line));
    assert(line != NULL && "Memory allocation failed for Line_New");

    // Initialize text gap buffer
    GapBuffer_Init(&line->text, initialCapacity, 64);

    // Initialize styles gap buffer (for syntax highlighting)
    GapBuffer_Init(&line->styles, initialCapacity, 64);

    // Initialize other fields
    line->lineNumber = 0;
    line->isFolded = false;
    line->foldLevel = 0;
    line->next = NULL;
    line->prev = NULL;

    return line;
}

void Line_Free(Line* line)
{
    if (!line)
        return;

    GapBuffer_Free(&line->text);
    GapBuffer_Free(&line->styles);
    free(line);
}

void Line_InsertChar(Line* line, size_t position, char character)
{
    assert(line != NULL && "Line cannot be NULL");
    assert(position <= GapBuffer_Size(&line->text) && "Insert position out of bounds");
    GapBuffer_InsertChar(&line->text, position, character);
}

void Line_DeleteChar(Line* line, size_t position)
{
    assert(line != NULL && "Line cannot be NULL");
    assert(position < GapBuffer_Size(&line->text) && "Delete position out of bounds");
    GapBuffer_Delete(&line->text, position, 1);
}

void Line_InsertText(Line* line, size_t position, const char* text, size_t length)
{
    assert(line != NULL && "Line cannot be NULL");
    assert(text != NULL && "Text pointer cannot be NULL");
    assert(position <= GapBuffer_Size(&line->text) && "Insert text position out of bounds");
    if (length == 0)
        return;

    Slice textSlice = Slice_Make(text, length);
    GapBuffer_InsertSlice(&line->text, position, textSlice);
}

void Line_DeleteText(Line* line, size_t position, size_t length)
{
    assert(line != NULL && "Line cannot be NULL");
    assert(position + length <= GapBuffer_Size(&line->text) && "Delete text range out of bounds");
    if (length == 0)
        return;

    GapBuffer_Delete(&line->text, position, length);
}

size_t Line_Length(Line* line)
{
    assert(line != NULL && "Line cannot be NULL");
    return GapBuffer_Size(&line->text);
}

Slice Line_GetText(Line* line)
{
    assert(line != NULL && "Line cannot be NULL");
    return GapBuffer_ToSlice(&line->text);
}

size_t Line_GetRenderX(Line* line, size_t cursorX, size_t tabSize)
{
    assert(line != NULL && "Line cannot be NULL");
    Slice text = GapBuffer_ToSlice(&line->text);
    size_t rx = 0;
    for (size_t i = 0; i < cursorX && i < text.size; i++) {
        assert(text.data != NULL && "Text data should not be NULL");
        if (((const char*)text.data)[i] == '\t')
            rx += (tabSize - 1) - (rx % tabSize);
        rx++;
    }
    return rx;
}
