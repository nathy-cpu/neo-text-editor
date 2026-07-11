#include "../neo.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Determines if two consecutive actions can be logically grouped into a single undo/redo step.
 * 
 * Grouping rules:
 * 1. Both actions must be of the same type.
 * 2. ACTION_INSERT_CHAR: The user is typing continuously. The next character must be 
 *    inserted immediately after the previous one on the same line.
 * 3. ACTION_DELETE_CHAR: The user is deleting continuously. 
 *    - Backspace: The cursor moves left, so the next deletion occurs one column before the previous.
 *    - Delete key: The cursor stays in place, so the next deletion occurs at the exact same column.
 * 
 * @param last The previous action recorded.
 * @param next The new action attempting to be recorded.
 * @return True if the actions should be grouped, false otherwise.
 */
static bool CanGroupActions(Action* last, Action* next)
{
    // We only group actions of the identical type (e.g. insertions with insertions)
    if (last->type != next->type)
        return false;

    // Check for continuous typing behavior
    if (last->type == ACTION_INSERT_CHAR) {
        return (last->lineNumber == next->lineNumber && last->column + 1 == next->column);
    }

    // Check for continuous deletion behavior
    if (last->type == ACTION_DELETE_CHAR) {
        // Backspace: column moves left by 1. Delete key: column stays the same.
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
    Array_Append(&group->actions, &action, 1);

    buffer->history.currentGroup = group;
    Stack_Push(&buffer->history.undoStack, group);
    Stack_EnforceLimit(&buffer->history.undoStack, buffer->history.undoLimit, (void (*)(void*))ActionGroup_Free);
}

Buffer* Buffer_New(void)
{
    Buffer* buffer = malloc(sizeof(Buffer));
    if (!buffer)
        return NULL;

    // Initialize empty buffer
    buffer->firstLine = NULL;
    buffer->lastLine = NULL;
    buffer->currentLine = NULL;
    buffer->totalLines = 0;
    buffer->totalBytes = 0;
    buffer->filename = NULL;
    buffer->isModified = false;
    buffer->isReadOnly = false;
    buffer->refCount = 1;

    // Initialize history
    History_Init(&buffer->history);

    // Create initial empty line
    Line* firstLine = Line_New(256);
    if (!firstLine) {
        free(buffer);
        return NULL;
    }

    buffer->firstLine = firstLine;
    buffer->lastLine = firstLine;
    buffer->currentLine = firstLine;
    buffer->totalLines = 1;

    return buffer;
}

void Buffer_Free(Buffer* buffer)
{
    if (!buffer)
        return;

    buffer->refCount--;
    if (buffer->refCount > 0)
        return;

    // Free history
    History_Free(&buffer->history);

    // Free all lines
    Line* current = buffer->firstLine;
    while (current) {
        Line* next = current->next;
        Line_Free(current);
        current = next;
    }

    free(buffer->filename);
    free(buffer);
}

Line* Buffer_GetLine(const Buffer* buffer, size_t lineNumber)
{
    assert(buffer);
    if (lineNumber >= buffer->totalLines)
        return NULL;

    // Traverse to the requested line
    Line* current = buffer->firstLine;
    for (size_t i = 0; i < lineNumber; i++) {
        assert(current != NULL && "Buffer line count mismatch");
        current = current->next;
    }
    assert(current != NULL && "Requested line should exist");
    return current;
}

Line* Buffer_InsertLine(Buffer* buffer, size_t lineNumber)
{
    assert(buffer && lineNumber <= buffer->totalLines);
    LOG_DEBUG("Buffer insert line at %zu (total: %zu)", lineNumber, buffer->totalLines);

    Line* newLine = Line_New(256);
    if (!newLine)
        return NULL;

    if (lineNumber == 0) {
        // Insert at beginning
        newLine->next = buffer->firstLine;
        if (buffer->firstLine) {
            buffer->firstLine->prev = newLine;
        }
        buffer->firstLine = newLine;
        if (!buffer->lastLine) {
            buffer->lastLine = newLine;
        }
    } else if (lineNumber == buffer->totalLines) {
        // Insert at end
        newLine->prev = buffer->lastLine;
        if (buffer->lastLine) {
            buffer->lastLine->next = newLine;
        }
        buffer->lastLine = newLine;
        if (!buffer->firstLine) {
            buffer->firstLine = newLine;
        }
    } else {
        // Insert in middle
        Line* target = Buffer_GetLine(buffer, lineNumber);
        assert(target != NULL && "Target line must exist");

        newLine->next = target;
        newLine->prev = target->prev;
        if (target->prev) {
            target->prev->next = newLine;
        }
        target->prev = newLine;

        if (target == buffer->firstLine) {
            buffer->firstLine = newLine;
        }
    }

    // Update line numbers
    Line* current = newLine->next;
    while (current) {
        current->lineNumber++;
        current = current->next;
    }

    newLine->lineNumber = lineNumber;
    buffer->totalLines++;
    buffer->isModified = true;

    return newLine;
}

void Buffer_DeleteLine(Buffer* buffer, size_t lineNumber)
{
    assert(buffer && lineNumber < buffer->totalLines);
    LOG_DEBUG("Buffer delete line at %zu (total: %zu)", lineNumber, buffer->totalLines);

    Line* line = Buffer_GetLine(buffer, lineNumber);
    assert(line != NULL && "Line to delete must exist");

    // Update linked list
    if (line->prev) {
        line->prev->next = line->next;
    } else {
        buffer->firstLine = line->next;
    }

    if (line->next) {
        line->next->prev = line->prev;
    } else {
        buffer->lastLine = line->prev;
    }

    // Update current line if needed
    if (buffer->currentLine == line) {
        buffer->currentLine = line->next ? line->next : line->prev;
    }

    // Update line numbers
    Line* current = line->next;
    while (current) {
        current->lineNumber--;
        current = current->next;
    }

    Line_Free(line);
    buffer->totalLines--;
    buffer->isModified = true;
}

void Buffer_InsertChar(Buffer* buffer, size_t lineNumber, size_t column, char character)
{
    assert(buffer);
    LOG_DEBUG("Buffer insert char '%c' (0x%02x) at %zu:%zu", (character >= 32 && character < 127) ? character : ' ',
        (unsigned char)character, lineNumber, column);
    Line* line = Buffer_GetLine(buffer, lineNumber);
    assert(line != NULL && "Line to insert char into must exist");

    Action action = {
        .type = ACTION_INSERT_CHAR, .lineNumber = lineNumber, .column = column, .payload = { .character = character }
    };
    RecordAction(buffer, action);

    Line_InsertChar(line, column, character);
    buffer->totalBytes++;
    buffer->isModified = true;
}

void Buffer_DeleteChar(Buffer* buffer, size_t lineNumber, size_t column)
{
    assert(buffer);
    LOG_DEBUG("Buffer delete char at %zu:%zu", lineNumber, column);
    Line* line = Buffer_GetLine(buffer, lineNumber);
    assert(line != NULL && "Line to delete char from must exist");

    if (column < Line_Length(line)) {
        char c = GapBuffer_Get(&line->text, column);
        Action action
            = { .type = ACTION_DELETE_CHAR, .lineNumber = lineNumber, .column = column, .payload = { .character = c } };
        RecordAction(buffer, action);

        Line_DeleteChar(line, column);
        buffer->totalBytes--;
        buffer->isModified = true;
    }
}

void Buffer_SplitLine(Buffer* buffer, size_t lineNumber, size_t column)
{
    assert(buffer);
    LOG_DEBUG("Buffer split line at %zu:%zu", lineNumber, column);
    Line* line = Buffer_GetLine(buffer, lineNumber);
    assert(line != NULL && "Line to split must exist");

    size_t lineLength = Line_Length(line);
    assert(column <= lineLength && "Split column must be within line length");
    if (column > lineLength)
        return;

    Action action = { .type = ACTION_SPLIT_LINE, .lineNumber = lineNumber, .column = column };
    RecordAction(buffer, action);

    // Create new line
    Line* newLine = Buffer_InsertLine(buffer, lineNumber + 1);
    assert(newLine != NULL && "Failed to create new line for split");

    // Move text from split point to new line
    Slice remainingText = GapBuffer_ToSlice(&line->text);
    if (column < remainingText.size) {
        Slice splitText = Slice_Subslice(remainingText, column, remainingText.size);
        Line_InsertText(newLine, 0, splitText.data, splitText.size);

        // Remove the moved text from original line
        Line_DeleteText(line, column, lineLength - column);
    }

    buffer->isModified = true;
}

void Buffer_JoinLine(Buffer* buffer, size_t lineNumber)
{
    assert(buffer && lineNumber + 1 < buffer->totalLines);
    LOG_DEBUG("Buffer join line at %zu", lineNumber);

    Line* currentLine = Buffer_GetLine(buffer, lineNumber);
    Line* nextLine = Buffer_GetLine(buffer, lineNumber + 1);
    assert(currentLine != NULL && nextLine != NULL && "Lines to join must exist");

    Action action = { .type = ACTION_JOIN_LINE, .lineNumber = lineNumber, .column = Line_Length(currentLine) };
    RecordAction(buffer, action);

    // Get text from next line
    Slice nextLineText = Line_GetText(nextLine);

    // Append to current line
    if (nextLineText.size > 0) {
        Line_InsertText(currentLine, Line_Length(currentLine), nextLineText.data, nextLineText.size);
    }

    // Delete the next line
    Buffer_DeleteLine(buffer, lineNumber + 1);

    buffer->isModified = true;
}

size_t Buffer_GetLineCount(const Buffer* buffer)
{
    assert(buffer);
    return buffer->totalLines;
}

size_t Buffer_GetTotalBytes(const Buffer* buffer)
{
    assert(buffer);
    return buffer->totalBytes;
}

Slice Buffer_ToSlice(const Buffer* buffer)
{
    assert(buffer);

    // Compute total size required including newlines
    size_t totalBytes = 0;
    Line* currentLine = buffer->firstLine;
    while (currentLine) {
        totalBytes += Line_Length(currentLine);
        if (currentLine->next) {
            totalBytes += 1; // '\n'
        }
        currentLine = currentLine->next;
    }

    if (totalBytes == 0) {
        return (Slice) { .data = NULL, .size = 0 };
    }

    char* content = malloc(totalBytes);
    assert(content);

    size_t offset = 0;
    currentLine = buffer->firstLine;
    while (currentLine) {
        Slice lineText = Line_GetText(currentLine);
        if (lineText.size > 0) {
            memcpy(content + offset, lineText.data, lineText.size);
            offset += lineText.size;
        }
        if (currentLine->next) {
            content[offset] = '\n';
            offset += 1;
        }
        currentLine = currentLine->next;
    }

    return (Slice) { .data = content, .size = totalBytes };
}

static void ApplyAction(Buffer* buffer, Action* action, bool undo)
{
    buffer->history.isUndoRedoing = true;
    if (undo) {
        switch (action->type) {
        case ACTION_INSERT_CHAR:
            Buffer_DeleteChar(buffer, action->lineNumber, action->column);
            break;
        case ACTION_DELETE_CHAR:
            Buffer_InsertChar(buffer, action->lineNumber, action->column, action->payload.character);
            break;
        case ACTION_SPLIT_LINE:
            Buffer_JoinLine(buffer, action->lineNumber);
            break;
        case ACTION_JOIN_LINE:
            Buffer_SplitLine(buffer, action->lineNumber, action->column);
            break;
        case ACTION_INSERT_TEXT:
        case ACTION_DELETE_TEXT:
        default:
            break;
        }
    } else {
        switch (action->type) {
        case ACTION_INSERT_CHAR:
            Buffer_InsertChar(buffer, action->lineNumber, action->column, action->payload.character);
            break;
        case ACTION_DELETE_CHAR:
            Buffer_DeleteChar(buffer, action->lineNumber, action->column);
            break;
        case ACTION_SPLIT_LINE:
            Buffer_SplitLine(buffer, action->lineNumber, action->column);
            break;
        case ACTION_JOIN_LINE:
            Buffer_JoinLine(buffer, action->lineNumber);
            break;
        case ACTION_INSERT_TEXT:
        case ACTION_DELETE_TEXT:
        default:
            break;
        }
    }
    buffer->history.isUndoRedoing = false;
}

void Buffer_Undo(Buffer* buffer)
{
    if (!buffer)
        return;
    buffer->history.currentGroup = NULL;
    ActionGroup* group = (ActionGroup*)Stack_Pop(&buffer->history.undoStack);
    if (!group)
        return;

    size_t count = Array_Size(&group->actions);
    for (size_t i = count; i > 0; i--) {
        Action* action = (Action*)Array_At(&group->actions, i - 1);
        ApplyAction(buffer, action, true);
    }

    Stack_Push(&buffer->history.redoStack, group);
    Stack_EnforceLimit(&buffer->history.redoStack, buffer->history.undoLimit, (void (*)(void*))ActionGroup_Free);
}

void Buffer_Redo(Buffer* buffer)
{
    if (!buffer)
        return;
    buffer->history.currentGroup = NULL;
    ActionGroup* group = (ActionGroup*)Stack_Pop(&buffer->history.redoStack);
    if (!group)
        return;

    size_t count = Array_Size(&group->actions);
    for (size_t i = 0; i < count; i++) {
        Action* action = (Action*)Array_At(&group->actions, i);
        ApplyAction(buffer, action, false);
    }

    Stack_Push(&buffer->history.undoStack, group);
    Stack_EnforceLimit(&buffer->history.undoStack, buffer->history.undoLimit, (void (*)(void*))ActionGroup_Free);
}
