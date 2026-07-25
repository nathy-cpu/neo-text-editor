#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "input.h"
#include "../core/buffer.h"
#include "../core/line.h"
#include "../utils/clipboard.h"
#include "../utils/logger.h"
#include "../utils/utf8.h"
#include "editor.h"
#include "explorer.h"
#include "logs_view.h"
#include "tab.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// A selection whose anchor equals the cursor selects nothing: treating it as
// real made cut clobber the clipboard with empty content and pushed empty
// undo groups that wiped the redo stack.
static bool Tab_HasNonEmptySelection(const Tab* tab)
{
    return tab->hasSelection && !(tab->selectStartX == tab->cursorX && tab->selectStartY == tab->cursorY);
}

static void Tab_ClearSelection(Tab* tab) { tab->hasSelection = false; }

// Codepoint-boundary steps over a line's bytes (UTF-8-aware cursor motion
// and deletion). Tolerant of malformed sequences: degrade to one byte.
static size_t Line_PreviousCodepointStart(Line* line, Buffer* buffer, size_t index)
{
    if (index == 0)
        return 0;
    size_t candidate = index - 1;
    size_t stepsBack = 0;
    while (candidate > 0 && stepsBack < 3 && Utf8IsContinuation((unsigned char)Line_GetChar(line, buffer, candidate))) {
        candidate--;
        stepsBack++;
    }
    unsigned char leadByte = (unsigned char)Line_GetChar(line, buffer, candidate);
    if (!Utf8IsContinuation(leadByte) && candidate + Utf8SequenceLength(leadByte) >= index)
        return candidate;
    return index - 1;
}

static size_t Line_NextCodepointStart(Line* line, Buffer* buffer, size_t index)
{
    size_t length = Line_Length(line);
    if (index >= length)
        return length;
    size_t next = index + Utf8SequenceLength((unsigned char)Line_GetChar(line, buffer, index));
    if (next > length)
        return length;
    for (size_t i = index + 1; i < next; i++) {
        if (!Utf8IsContinuation((unsigned char)Line_GetChar(line, buffer, i)))
            return i;
    }
    return next;
}

void Editor_MoveCursor(Editor* editor, int key)
{
    if (Array_Size(&editor->tabs) == 0)
        return;
    Tab* tab = Array_Get(&editor->tabs, Tab*, editor->activeTabIndex);
    Line* row = Buffer_GetLine(tab->buffer, tab->cursorY);

    switch (key) {
    case ARROW_LEFT:
        if (tab->cursorX != 0)
            tab->cursorX = row ? Line_PreviousCodepointStart(row, tab->buffer, tab->cursorX) : tab->cursorX - 1;
        else {
            size_t prevVisible = Tab_PrevVisibleLine(tab, tab->cursorY);
            if (prevVisible < tab->cursorY) {
                tab->cursorY = prevVisible;
                Line* previousRow = Buffer_GetLine(tab->buffer, tab->cursorY);
                tab->cursorX = previousRow ? Line_Length(previousRow) : 0;
            }
        }
        break;
    case ARROW_RIGHT:
        if (row != NULL && tab->cursorX < Line_Length(row))
            tab->cursorX = Line_NextCodepointStart(row, tab->buffer, tab->cursorX);
        else if (row != NULL && tab->cursorX == Line_Length(row)) {
            size_t nextVisible = Tab_NextVisibleLine(tab, tab->cursorY);
            if (nextVisible > tab->cursorY) {
                tab->cursorY = nextVisible;
                tab->cursorX = 0;
            }
        }
        break;
    case ARROW_UP: {
        size_t currentVRowIdx = Tab_GetCursorVRowIdx(tab);
        if (currentVRowIdx > 0) {
            size_t targetCol = Tab_GetCursorVisualCol(tab, currentVRowIdx);
            Tab_SetCursorFromVRow(tab, currentVRowIdx - 1, targetCol);
        }
        break;
    }
    case ARROW_DOWN: {
        size_t currentVRowIdx = Tab_GetCursorVRowIdx(tab);
        if (currentVRowIdx + 1 < Tab_GetVisualRowCount(tab)) {
            size_t targetCol = Tab_GetCursorVisualCol(tab, currentVRowIdx);
            Tab_SetCursorFromVRow(tab, currentVRowIdx + 1, targetCol);
        }
        break;
    }
    }

    row = Buffer_GetLine(tab->buffer, tab->cursorY);
    size_t rowSize = (row != NULL) ? Line_Length(row) : 0;
    if (tab->cursorX > rowSize)
        tab->cursorX = rowSize;
    else if (row && tab->cursorX > 0 && tab->cursorX < rowSize
        && Utf8IsContinuation((unsigned char)Line_GetChar(row, tab->buffer, tab->cursorX)))
        tab->cursorX = Line_PreviousCodepointStart(row, tab->buffer, tab->cursorX);
}

void Tab_GetSelection(Tab* tab, size_t* startX, size_t* startY, size_t* endX, size_t* endY)
{
    if (tab->selectStartY < tab->cursorY || (tab->selectStartY == tab->cursorY && tab->selectStartX < tab->cursorX)) {
        *startX = tab->selectStartX;
        *startY = tab->selectStartY;
        *endX = tab->cursorX;
        *endY = tab->cursorY;
    } else {
        *startX = tab->cursorX;
        *startY = tab->cursorY;
        *endX = tab->selectStartX;
        *endY = tab->selectStartY;
    }
}

char* Tab_GetSelectedText(Tab* tab)
{
    if (!tab->hasSelection)
        return NULL;
    size_t startX, startY, endX, endY;
    Tab_GetSelection(tab, &startX, &startY, &endX, &endY);

    size_t totalLen = 0;
    if (startY == endY) {
        Line* line = Buffer_GetLine(tab->buffer, startY);
        if (line) {
            size_t len = Line_Length(line);
            if (startX < len) {
                size_t actualX = (endX > len) ? len : endX;
                totalLen += (actualX - startX);
            }
        }
    } else {
        // First line
        Line* firstLine = Buffer_GetLine(tab->buffer, startY);
        if (firstLine) {
            size_t len = Line_Length(firstLine);
            if (startX < len) {
                totalLen += (len - startX);
            }
        }
        totalLen++; // for newline

        // Middle lines
        for (size_t i = startY + 1; i < endY; i++) {
            Line* midLine = Buffer_GetLine(tab->buffer, i);
            if (midLine) {
                totalLen += Line_Length(midLine);
            }
            totalLen++; // for newline
        }

        // Last line
        Line* lastLine = Buffer_GetLine(tab->buffer, endY);
        if (lastLine) {
            size_t len = Line_Length(lastLine);
            size_t actualX = (endX > len) ? len : endX;
            totalLen += actualX;
        }
    }

    char* result = malloc(totalLen + 1);
    if (!result)
        return NULL;

    size_t offset = 0;
    if (startY == endY) {
        Line* line = Buffer_GetLine(tab->buffer, startY);
        if (line) {
            size_t len = Line_Length(line);
            if (startX < len) {
                size_t actualX = (endX > len) ? len : endX;
                size_t segmentLen = actualX - startX;
                Slice s = Line_GetTextRange(line, tab->buffer, startX, segmentLen);
                memcpy(result + offset, s.data, segmentLen);
                offset += segmentLen;
            }
        }
    } else {
        // First line
        Line* firstLine = Buffer_GetLine(tab->buffer, startY);
        if (firstLine) {
            size_t len = Line_Length(firstLine);
            if (startX < len) {
                size_t segmentLen = len - startX;
                Slice s = Line_GetTextRange(firstLine, tab->buffer, startX, segmentLen);
                memcpy(result + offset, s.data, segmentLen);
                offset += segmentLen;
            }
        }
        result[offset++] = '\n';

        // Middle lines
        for (size_t i = startY + 1; i < endY; i++) {
            Line* midLine = Buffer_GetLine(tab->buffer, i);
            if (midLine) {
                size_t segmentLen = Line_Length(midLine);
                Slice s = Line_GetText(midLine, tab->buffer);
                memcpy(result + offset, s.data, segmentLen);
                offset += segmentLen;
            }
            result[offset++] = '\n';
        }

        // Last line
        Line* lastLine = Buffer_GetLine(tab->buffer, endY);
        if (lastLine) {
            size_t len = Line_Length(lastLine);
            size_t actualX = (endX > len) ? len : endX;
            Slice s = Line_GetTextRange(lastLine, tab->buffer, 0, actualX);
            memcpy(result + offset, s.data, actualX);
            offset += actualX;
        }
    }

    result[offset] = '\0';
    return result;
}

void Tab_CopySelection(Tab* tab)
{
    if (!tab->hasSelection)
        return;
    size_t startX, startY, endX, endY;
    Tab_GetSelection(tab, &startX, &startY, &endX, &endY);

    Clipboard_BeginWrite();

    if (startY == endY) {
        Line* line = Buffer_GetLine(tab->buffer, startY);
        if (line) {
            size_t len = Line_Length(line);
            if (startX < len) {
                size_t actualX = (endX > len) ? len : endX;
                size_t segmentLen = actualX - startX;
                Slice s = Line_GetTextRange(line, tab->buffer, startX, segmentLen);
                Clipboard_Append((const char*)s.data, segmentLen);
            }
        }
    } else {
        // First line
        Line* firstLine = Buffer_GetLine(tab->buffer, startY);
        if (firstLine) {
            size_t len = Line_Length(firstLine);
            if (startX < len) {
                size_t segmentLen = len - startX;
                Slice s = Line_GetTextRange(firstLine, tab->buffer, startX, segmentLen);
                Clipboard_Append((const char*)s.data, segmentLen);
            }
        }
        Clipboard_Append("\n", 1);

        // Middle lines
        for (size_t i = startY + 1; i < endY; i++) {
            Line* midLine = Buffer_GetLine(tab->buffer, i);
            if (midLine) {
                size_t segmentLen = Line_Length(midLine);
                Slice s = Line_GetText(midLine, tab->buffer);
                Clipboard_Append((const char*)s.data, segmentLen);
            }
            Clipboard_Append("\n", 1);
        }

        // Last line
        Line* lastLine = Buffer_GetLine(tab->buffer, endY);
        if (lastLine) {
            size_t len = Line_Length(lastLine);
            size_t actualX = (endX > len) ? len : endX;
            Slice s = Line_GetTextRange(lastLine, tab->buffer, 0, actualX);
            Clipboard_Append((const char*)s.data, actualX);
        }
    }

    Clipboard_EndWrite();
}

void Editor_DeleteSelection(Editor* editor)
{
    if (Array_Size(&editor->tabs) == 0)
        return;
    Tab* tab = Array_Get(&editor->tabs, Tab*, editor->activeTabIndex);
    if (tab->buffer->isReadOnly)
        return;
    if (!Tab_HasNonEmptySelection(tab)) {
        Tab_ClearSelection(tab);
        return;
    }
    size_t startX, startY, endX, endY;
    Tab_GetSelection(tab, &startX, &startY, &endX, &endY);

    Buffer* buffer = tab->buffer;
    Line* startLine = Buffer_GetLine(buffer, startY);
    Line* endLine = Buffer_GetLine(buffer, endY);
    if (!startLine || !endLine) {
        Tab_ClearSelection(tab);
        return;
    }
    // Stale coordinates can exceed the line (Ctrl+A leaves cursorX at length).
    if (startX > Line_Length(startLine))
        startX = Line_Length(startLine);
    if (endX > Line_Length(endLine))
        endX = Line_Length(endLine);

    size_t startOffset = startLine->offset + startX;
    size_t endOffset = endLine->offset + endX;
    if (endOffset <= startOffset) {
        Tab_ClearSelection(tab);
        return;
    }

    // One composite action + one range delete: O(range) instead of one
    // action and a full piece-table rebuild per deleted byte. Deliberately
    // group-transparent -- when a caller (paste/type-over) already opened a
    // composite group with forceGrouping, this joins it so the whole
    // replace-selection operation undoes in a single step.
    Buffer_RecordCompositeEdit(buffer, startY, startX);
    Buffer_DeleteRange(buffer, startOffset, endOffset);

    tab->cursorY = startY;
    tab->cursorX = startX;
    Tab_ClearSelection(tab);
    tab->isSaved = false;
}

void Editor_MoveCursorWord(Editor* editor, int key)
{
    if (Array_Size(&editor->tabs) == 0)
        return;
    Tab* tab = Array_Get(&editor->tabs, Tab*, editor->activeTabIndex);
    Line* row = Buffer_GetLine(tab->buffer, tab->cursorY);
    if (!row)
        return;

    if (key == CTRL_ARROW_RIGHT) {
        while (tab->cursorX < Line_Length(row)) {
            char c = Line_GetChar(row, tab->buffer, tab->cursorX);
            if (ByteIsSpace(c))
                break;
            tab->cursorX++;
        }
        while (tab->cursorX < Line_Length(row)) {
            char c = Line_GetChar(row, tab->buffer, tab->cursorX);
            if (!ByteIsSpace(c))
                break;
            tab->cursorX++;
        }
        if (tab->cursorX == Line_Length(row) && tab->cursorY < Buffer_GetLineCount(tab->buffer) - 1) {
            tab->cursorY++;
            tab->cursorX = 0;
        }
    } else if (key == CTRL_ARROW_LEFT) {
        if (tab->cursorX == 0 && tab->cursorY > 0) {
            tab->cursorY--;
            row = Buffer_GetLine(tab->buffer, tab->cursorY);
            tab->cursorX = row ? Line_Length(row) : 0;
            return;
        }
        while (tab->cursorX > 0) {
            char c = Line_GetChar(row, tab->buffer, tab->cursorX - 1);
            if (!ByteIsSpace(c))
                break;
            tab->cursorX--;
        }
        while (tab->cursorX > 0) {
            char c = Line_GetChar(row, tab->buffer, tab->cursorX - 1);
            if (ByteIsSpace(c))
                break;
            tab->cursorX--;
        }
    }
}

// Moves the cursor one screen's worth of VISUAL rows (wrap- and fold-aware).
// The old code assigned tab->rowOffset (a visual-row index) to tab->cursorY
// (a line index), teleporting the cursor whenever wrapping made them diverge,
// and PAGE_UP had no clamp at all.
static void Editor_PageMove(Editor* editor, Tab* tab, int direction)
{
    size_t currentVisualRow = Tab_GetCursorVRowIdx(tab);
    size_t targetColumn = Tab_GetCursorVisualCol(tab, currentVisualRow);
    size_t page = editor->screenRows > 0 ? editor->screenRows : 1;
    size_t totalVisualRows = Tab_GetVisualRowCount(tab);

    size_t newVisualRow;
    if (direction < 0) {
        newVisualRow = currentVisualRow > page ? currentVisualRow - page : 0;
    } else {
        newVisualRow = currentVisualRow + page;
        if (totalVisualRows == 0)
            newVisualRow = 0;
        else if (newVisualRow >= totalVisualRows)
            newVisualRow = totalVisualRows - 1;
    }
    Tab_SetCursorFromVRow(tab, newVisualRow, targetColumn);
}

void Editor_ProcessInput(Editor* editor, int input)
{
    if (Array_Size(&editor->tabs) == 0)
        return;
    Tab* tab = Array_Get(&editor->tabs, Tab*, editor->activeTabIndex);
    bool modified = false;

    LOG_DEBUG(
        "Editor_ProcessInput: processing keypress %d on tab '%s'", input, tab->filename ? tab->filename : "<scratch>");

    if (input == editor->config.keySave) {
        if (tab->buffer->isReadOnly) {
            Editor_SetStatusMessage(editor, "Error: File is read-only");
        } else {
            Tab_SaveFile(tab);
            Editor_SetStatusMessage(editor, "File saved.");
        }
    } else if (input == editor->config.keyToggleFold) {
        Editor_ToggleFold(editor);
    } else if (input == editor->config.keyToggleAllFolds) {
        Editor_ToggleAllFolds(editor);
    } else if (input == editor->config.keyCopy) {
        if (Tab_HasNonEmptySelection(tab)) {
            Tab_CopySelection(tab);
            Editor_SetStatusMessage(editor, "Copied selection to clipboard");
        } else {
            Editor_SetStatusMessage(editor, "No selection to copy");
        }
    } else if (input == editor->config.keyCut) {
        if (tab->buffer->isReadOnly) {
            Editor_SetStatusMessage(editor, "Error: File is read-only");
        } else if (Tab_HasNonEmptySelection(tab)) {
            Tab_CopySelection(tab);
            Editor_DeleteSelection(editor);
            Editor_SetStatusMessage(editor, "Cut selection to clipboard");
            modified = true;
        } else {
            Editor_SetStatusMessage(editor, "No selection to cut");
        }
    } else if (input == editor->config.keyPaste) {
        if (tab->buffer->isReadOnly) {
            Editor_SetStatusMessage(editor, "Error: File is read-only");
        } else {
            Slice s = Clipboard_Read();
            if (s.size > 0 && s.data) {
                // Open the composite group BEFORE deleting the selection so
                // delete + insert undo as one step (no intermediate state).
                Buffer_RecordCompositeEdit(tab->buffer, tab->cursorY, tab->cursorX);
                tab->buffer->history.forceGrouping = true;
                if (Tab_HasNonEmptySelection(tab)) {
                    Editor_DeleteSelection(editor);
                }
                const char* text = (const char*)s.data;
                size_t len = s.size;
                for (size_t i = 0; i < len; i++) {
                    char c = text[i];
                    if (c == '\r') {
                        continue;
                    } else if (c == '\n') {
                        Buffer_SplitLine(tab->buffer, tab->cursorY, tab->cursorX);
                        tab->cursorY++;
                        tab->cursorX = 0;
                    } else {
                        Buffer_InsertChar(tab->buffer, tab->cursorY, tab->cursorX, c);
                        tab->cursorX++;
                    }
                }
                tab->buffer->history.forceGrouping = false;
                tab->buffer->history.currentGroup = NULL;
                tab->isSaved = false;
                modified = true;
                Editor_SetStatusMessage(editor, "Pasted from clipboard");
            } else {
                Editor_SetStatusMessage(editor, "Clipboard is empty");
            }
        }
    } else if (input == editor->config.keyDeleteLine) {
        if (tab->buffer->isReadOnly) {
            Editor_SetStatusMessage(editor, "Error: File is read-only");
        } else {
            Editor_DeleteLine(editor);
            modified = true;
        }
    } else if (input == editor->config.keyKillToEnd) {
        if (tab->buffer->isReadOnly) {
            Editor_SetStatusMessage(editor, "Error: File is read-only");
        } else {
            Editor_KillToEndOfLine(editor);
            modified = true;
        }
    } else if (input == editor->config.keyJoinLines) {
        if (tab->buffer->isReadOnly) {
            Editor_SetStatusMessage(editor, "Error: File is read-only");
        } else {
            Editor_JoinLines(editor);
            modified = true;
        }
    } else if (input == editor->config.keyMoveLineUp) {
        if (tab->buffer->isReadOnly) {
            Editor_SetStatusMessage(editor, "Error: File is read-only");
        } else {
            Editor_MoveLine(editor, -1);
            modified = true;
        }
    } else if (input == editor->config.keyMoveLineDown) {
        if (tab->buffer->isReadOnly) {
            Editor_SetStatusMessage(editor, "Error: File is read-only");
        } else {
            Editor_MoveLine(editor, 1);
            modified = true;
        }
    } else {
        switch (input) {
        case '\t':
            if (tab->buffer->isReadOnly) {
                Editor_SetStatusMessage(editor, "Error: File is read-only");
                break;
            }
            if (tab->hasSelection) {
                Editor_IndentLines(editor);
                modified = true;
            } else {
                Buffer_InsertChar(tab->buffer, tab->cursorY, tab->cursorX, '\t');
                tab->cursorX++;
                tab->isSaved = false;
                modified = true;
            }
            break;

        case SHIFT_TAB:
            if (tab->buffer->isReadOnly) {
                Editor_SetStatusMessage(editor, "Error: File is read-only");
                break;
            }
            Editor_UnindentLines(editor);
            modified = true;
            break;

        case '\r':
            if (tab->buffer->isReadOnly) {
                Editor_SetStatusMessage(editor, "Error: File is read-only");
                break;
            }
            if (Tab_HasNonEmptySelection(tab)) {
                Buffer_RecordCompositeEdit(tab->buffer, tab->cursorY, tab->cursorX);
                tab->buffer->history.forceGrouping = true;
                Editor_DeleteSelection(editor);
                Buffer_SplitLine(tab->buffer, tab->cursorY, tab->cursorX);
                tab->buffer->history.forceGrouping = false;
                tab->buffer->history.currentGroup = NULL;
            } else {
                Tab_ClearSelection(tab);
                Buffer_SplitLine(tab->buffer, tab->cursorY, tab->cursorX);
            }
            tab->cursorY++;
            tab->cursorX = 0;
            tab->isSaved = false;
            modified = true;
            break;

        case CTRL_KEY('a'):
            tab->selectStartX = 0;
            tab->selectStartY = 0;
            tab->cursorY = Buffer_GetLineCount(tab->buffer) > 0 ? Buffer_GetLineCount(tab->buffer) - 1 : 0;
            Line* lastRow = Buffer_GetLine(tab->buffer, tab->cursorY);
            tab->cursorX = lastRow ? Line_Length(lastRow) : 0;
            tab->hasSelection = true;
            break;

        case CTRL_KEY('z'):
            if (!tab->buffer->isReadOnly) {
                Tab_ClearSelection(tab); // the selected region no longer describes this content
                size_t undoLine, undoColumn;
                if (Buffer_Undo(tab->buffer, &undoLine, &undoColumn)) {
                    tab->cursorY = undoLine;
                    tab->cursorX = undoColumn;
                    modified = true;
                    tab->isSaved = false;
                    if (tab->cursorY >= Buffer_GetLineCount(tab->buffer)) {
                        tab->cursorY = Buffer_GetLineCount(tab->buffer) > 0 ? Buffer_GetLineCount(tab->buffer) - 1 : 0;
                    }
                    Line* rowZ = Buffer_GetLine(tab->buffer, tab->cursorY);
                    if (rowZ && tab->cursorX > Line_Length(rowZ)) {
                        tab->cursorX = Line_Length(rowZ);
                    }
                    Buffer_EnsureLineVisible(tab->buffer, tab->cursorY, tab->config->tabSize);
                }
            }
            break;

        case CTRL_KEY('y'):
            if (!tab->buffer->isReadOnly) {
                Tab_ClearSelection(tab);
                size_t redoLine, redoColumn;
                if (Buffer_Redo(tab->buffer, &redoLine, &redoColumn)) {
                    tab->cursorY = redoLine;
                    tab->cursorX = redoColumn;
                    modified = true;
                    tab->isSaved = false;
                    if (tab->cursorY >= Buffer_GetLineCount(tab->buffer)) {
                        tab->cursorY = Buffer_GetLineCount(tab->buffer) > 0 ? Buffer_GetLineCount(tab->buffer) - 1 : 0;
                    }
                    Line* rowY = Buffer_GetLine(tab->buffer, tab->cursorY);
                    if (rowY && tab->cursorX > Line_Length(rowY)) {
                        tab->cursorX = Line_Length(rowY);
                    }
                    Buffer_EnsureLineVisible(tab->buffer, tab->cursorY, tab->config->tabSize);
                }
            }
            break;

        case CTRL_KEY('q'):
            // Handled in Editor_ProcessKeypress
            break;

        case RESIZE_EVENT:
            // Handled in Editor_ProcessKeypress
            break;

        case HOME_KEY:
            tab->hasSelection = false;
            tab->cursorX = 0;
            break;

        case END_KEY: {
            tab->hasSelection = false;
            Line* row = Buffer_GetLine(tab->buffer, tab->cursorY);
            if (row)
                tab->cursorX = Line_Length(row);
        } break;

        case CTRL_HOME_KEY:
            tab->hasSelection = false;
            tab->cursorY = 0;
            tab->cursorX = 0;
            break;

        case CTRL_END_KEY: {
            tab->hasSelection = false;
            size_t lineCount = Buffer_GetLineCount(tab->buffer);
            tab->cursorY = lineCount > 0 ? lineCount - 1 : 0;
            tab->cursorX = 0;
        } break;

        case BACKSPACE:
        case CTRL_KEY('h'):
        case DELETE_KEY:
            if (tab->buffer->isReadOnly) {
                Editor_SetStatusMessage(editor, "Error: File is read-only");
                break;
            }
            if (Tab_HasNonEmptySelection(tab)) {
                Editor_DeleteSelection(editor);
                modified = true;
            } else {
                Tab_ClearSelection(tab);
                if (input == DELETE_KEY) {
                    Line* currLine = Buffer_GetLine(tab->buffer, tab->cursorY);
                    if (currLine) {
                        if (tab->cursorX < Line_Length(currLine)) {
                            // Remove the whole codepoint, not one byte -- a
                            // partial delete leaves invalid UTF-8 that gets
                            // written to disk on save.
                            size_t nextStart = Line_NextCodepointStart(currLine, tab->buffer, tab->cursorX);
                            if (nextStart - tab->cursorX == 1) {
                                Buffer_DeleteChar(tab->buffer, tab->cursorY, tab->cursorX);
                            } else {
                                Buffer_RecordCompositeEdit(tab->buffer, tab->cursorY, tab->cursorX);
                                Buffer_DeleteRange(
                                    tab->buffer, currLine->offset + tab->cursorX, currLine->offset + nextStart);
                            }
                            tab->isSaved = false;
                            modified = true;
                        } else if (tab->cursorY < Buffer_GetLineCount(tab->buffer) - 1) {
                            Buffer_JoinLine(tab->buffer, tab->cursorY);
                            tab->isSaved = false;
                            modified = true;
                        }
                    }
                } else {
                    if (tab->cursorX > 0) {
                        Line* currLine = Buffer_GetLine(tab->buffer, tab->cursorY);
                        size_t previousStart = currLine
                            ? Line_PreviousCodepointStart(currLine, tab->buffer, tab->cursorX)
                            : tab->cursorX - 1;
                        if (tab->cursorX - previousStart == 1) {
                            Buffer_DeleteChar(tab->buffer, tab->cursorY, tab->cursorX - 1);
                        } else {
                            Buffer_RecordCompositeEdit(tab->buffer, tab->cursorY, previousStart);
                            Buffer_DeleteRange(
                                tab->buffer, currLine->offset + previousStart, currLine->offset + tab->cursorX);
                        }
                        tab->cursorX = previousStart;
                        tab->isSaved = false;
                        modified = true;
                    } else if (tab->cursorY > 0) {
                        Line* previousRow = Buffer_GetLine(tab->buffer, tab->cursorY - 1);
                        size_t previousLength = previousRow ? Line_Length(previousRow) : 0;
                        Buffer_JoinLine(tab->buffer, tab->cursorY - 1);
                        tab->cursorY--;
                        tab->cursorX = previousLength;
                        tab->isSaved = false;
                        modified = true;
                    }
                }
            }
            break;

        case CTRL_BACKSPACE:
        case ALT_BACKSPACE:
            if (tab->buffer->isReadOnly) {
                Editor_SetStatusMessage(editor, "Error: File is read-only");
                break;
            }
            if (Tab_HasNonEmptySelection(tab)) {
                Editor_DeleteSelection(editor);
                modified = true;
            } else {
                Tab_ClearSelection(tab);
                Editor_DeleteWord(editor, -1);
                modified = true;
            }
            break;

        case CTRL_DELETE:
        case ALT_DELETE:
            if (tab->buffer->isReadOnly) {
                Editor_SetStatusMessage(editor, "Error: File is read-only");
                break;
            }
            if (Tab_HasNonEmptySelection(tab)) {
                Editor_DeleteSelection(editor);
                modified = true;
            } else {
                Tab_ClearSelection(tab);
                Editor_DeleteWord(editor, 1);
                modified = true;
            }
            break;

        case CTRL_KEY('k'):
            if (tab->buffer->isReadOnly) {
                Editor_SetStatusMessage(editor, "Error: File is read-only");
                break;
            }
            if (Tab_HasNonEmptySelection(tab)) {
                Editor_DeleteSelection(editor);
                modified = true;
            } else {
                Tab_ClearSelection(tab);
                Editor_KillToEndOfLine(editor);
                modified = true;
            }
            break;

        case CTRL_KEY('j'):
            if (tab->buffer->isReadOnly) {
                Editor_SetStatusMessage(editor, "Error: File is read-only");
                break;
            }
            Editor_JoinLines(editor);
            modified = true;
            break;

        case CTRL_KEY('d'):
            if (tab->buffer->isReadOnly) {
                Editor_SetStatusMessage(editor, "Error: File is read-only");
                break;
            }
            Editor_DeleteLine(editor);
            modified = true;
            break;

        case PAGE_UP:
        case PAGE_DOWN:
            Tab_ClearSelection(tab); // match arrows: plain paging drops the selection
            Editor_PageMove(editor, tab, input == PAGE_UP ? -1 : 1);
            break;

        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
            tab->hasSelection = false;
            Editor_MoveCursor(editor, input);
            break;

        case CTRL_ARROW_LEFT:
        case CTRL_ARROW_RIGHT:
            tab->hasSelection = false;
            Editor_MoveCursorWord(editor, input);
            break;

        case ALT_ARROW_UP:
            if (tab->buffer->isReadOnly) {
                Editor_SetStatusMessage(editor, "Error: File is read-only");
                break;
            }
            Editor_MoveLine(editor, -1);
            modified = true;
            break;

        case ALT_ARROW_DOWN:
            if (tab->buffer->isReadOnly) {
                Editor_SetStatusMessage(editor, "Error: File is read-only");
                break;
            }
            Editor_MoveLine(editor, 1);
            modified = true;
            break;

        case CTRL_ENTER:
            if (tab->buffer->isReadOnly) {
                Editor_SetStatusMessage(editor, "Error: File is read-only");
                break;
            }
            Editor_InsertLineBelow(editor);
            modified = true;
            break;

        case CTRL_SHIFT_ENTER:
            if (tab->buffer->isReadOnly) {
                Editor_SetStatusMessage(editor, "Error: File is read-only");
                break;
            }
            Editor_InsertLineAbove(editor);
            modified = true;
            break;

        case SHIFT_ARROW_UP:
        case SHIFT_ARROW_DOWN:
        case SHIFT_ARROW_LEFT:
        case SHIFT_ARROW_RIGHT:
        case SHIFT_HOME_KEY:
        case SHIFT_END_KEY:
        case SHIFT_CTRL_LEFT:
        case SHIFT_CTRL_RIGHT:
        case SHIFT_CTRL_HOME:
        case SHIFT_CTRL_END:
        case SHIFT_PAGE_UP:
        case SHIFT_PAGE_DOWN:
            if (!tab->hasSelection) {
                tab->hasSelection = true;
                tab->selectStartX = tab->cursorX;
                tab->selectStartY = tab->cursorY;
            }
            if (input == SHIFT_ARROW_UP)
                Editor_MoveCursor(editor, ARROW_UP);
            else if (input == SHIFT_ARROW_DOWN)
                Editor_MoveCursor(editor, ARROW_DOWN);
            else if (input == SHIFT_ARROW_LEFT)
                Editor_MoveCursor(editor, ARROW_LEFT);
            else if (input == SHIFT_ARROW_RIGHT)
                Editor_MoveCursor(editor, ARROW_RIGHT);
            else if (input == SHIFT_HOME_KEY)
                tab->cursorX = 0;
            else if (input == SHIFT_END_KEY) {
                Line* row = Buffer_GetLine(tab->buffer, tab->cursorY);
                if (row)
                    tab->cursorX = Line_Length(row);
            } else if (input == SHIFT_CTRL_HOME) {
                tab->cursorY = 0;
                tab->cursorX = 0;
            } else if (input == SHIFT_CTRL_END) {
                size_t lineCount = Buffer_GetLineCount(tab->buffer);
                tab->cursorY = lineCount > 0 ? lineCount - 1 : 0;
                tab->cursorX = 0;
            } else if (input == SHIFT_CTRL_LEFT) {
                Editor_MoveCursorWord(editor, CTRL_ARROW_LEFT);
            } else if (input == SHIFT_CTRL_RIGHT) {
                Editor_MoveCursorWord(editor, CTRL_ARROW_RIGHT);
            } else if (input == SHIFT_PAGE_UP) {
                Editor_PageMove(editor, tab, -1);
            } else if (input == SHIFT_PAGE_DOWN) {
                Editor_PageMove(editor, tab, 1);
            }
            break;

        case CTRL_KEY('l'):
        case '\x1b':
            break;

        default:
            // Printable ASCII plus raw UTF-8 bytes (128..255): multi-byte
            // sequences arrive as consecutive keypresses and the buffer is a
            // byte store, so inserting them byte-wise composes correctly.
            // The old `input < 128` filter made non-ASCII untypeable.
            if (input >= 32 && input < 256 && input != 127) {
                if (tab->buffer->isReadOnly) {
                    Editor_SetStatusMessage(editor, "Error: File is read-only");
                    break;
                }
                if (Tab_HasNonEmptySelection(tab)) {
                    // One composite group: replacing a selection undoes in a
                    // single step instead of exposing the deleted-only state.
                    Buffer_RecordCompositeEdit(tab->buffer, tab->cursorY, tab->cursorX);
                    tab->buffer->history.forceGrouping = true;
                    Editor_DeleteSelection(editor);
                    Buffer_InsertChar(tab->buffer, tab->cursorY, tab->cursorX, input);
                    tab->buffer->history.forceGrouping = false;
                    tab->buffer->history.currentGroup = NULL;
                } else {
                    Tab_ClearSelection(tab);
                    Buffer_InsertChar(tab->buffer, tab->cursorY, tab->cursorX, input);
                }
                tab->cursorX++;
                tab->isSaved = false;
                modified = true;
            }
            break;
        }
    }

    if (modified) {
        Buffer_EnsureLineVisible(tab->buffer, tab->cursorY, tab->config->tabSize);
        Tab_UpdateSyntax(tab, SIZE_MAX);
    }
}

void Editor_EnsureSelection(Editor* editor)
{
    if (Array_Size(&editor->tabs) == 0)
        return;
    Tab* tab = Array_Get(&editor->tabs, Tab*, editor->activeTabIndex);
    if (!tab->hasSelection) {
        tab->hasSelection = true;
        tab->selectStartX = tab->cursorX;
        tab->selectStartY = tab->cursorY;
    }
}

void Editor_DeleteWord(Editor* editor, int direction)
{
    if (Array_Size(&editor->tabs) == 0)
        return;
    Tab* tab = Array_Get(&editor->tabs, Tab*, editor->activeTabIndex);
    if (tab->buffer->isReadOnly)
        return;

    Buffer* buffer = tab->buffer;
    size_t y = tab->cursorY;
    size_t x = tab->cursorX;
    Line* row = Buffer_GetLine(buffer, y);
    if (!row)
        return;

    if (direction < 0) {
        if (x == 0) {
            if (y == 0) {
                return;
            }
            Buffer_JoinLine(buffer, y - 1);
            Line* prevRow = Buffer_GetLine(buffer, y - 1);
            tab->cursorY = y - 1;
            tab->cursorX = prevRow ? Line_Length(prevRow) : 0;
        } else {
            size_t wordStart = x;
            while (wordStart > 0 && ByteIsSpace(Line_GetChar(row, buffer, wordStart - 1)))
                wordStart--;
            while (wordStart > 0 && !ByteIsSpace(Line_GetChar(row, buffer, wordStart - 1)))
                wordStart--;
            size_t startOff = row->offset + wordStart;
            size_t endOff = row->offset + x;
            Buffer_RecordCompositeEdit(buffer, y, wordStart);
            Buffer_DeleteRange(buffer, startOff, endOff);
            tab->cursorX = wordStart;
        }
    } else {
        size_t lineLen = Line_Length(row);
        if (x == lineLen) {
            if (y >= Buffer_GetLineCount(buffer) - 1) {
                return;
            }
            Buffer_JoinLine(buffer, y);
        } else {
            size_t wordEnd = x;
            while (wordEnd < lineLen && !ByteIsSpace(Line_GetChar(row, buffer, wordEnd)))
                wordEnd++;
            while (wordEnd < lineLen && ByteIsSpace(Line_GetChar(row, buffer, wordEnd)))
                wordEnd++;
            size_t startOff = row->offset + x;
            size_t endOff = row->offset + wordEnd;
            Buffer_RecordCompositeEdit(buffer, y, x);
            Buffer_DeleteRange(buffer, startOff, endOff);
        }
    }

    buffer->isModified = true;
    Tab_ClearSelection(tab);
    tab->isSaved = false;
}

void Editor_KillToEndOfLine(Editor* editor)
{
    if (Array_Size(&editor->tabs) == 0)
        return;
    Tab* tab = Array_Get(&editor->tabs, Tab*, editor->activeTabIndex);
    if (tab->buffer->isReadOnly)
        return;

    Buffer* buffer = tab->buffer;
    size_t y = tab->cursorY;
    size_t x = tab->cursorX;
    Line* row = Buffer_GetLine(buffer, y);
    if (!row)
        return;

    size_t lineLen = Line_Length(row);

    if (x < lineLen) {
        size_t startOff = row->offset + x;
        size_t endOff = row->offset + lineLen;
        Buffer_RecordCompositeEdit(buffer, y, x);
        Buffer_DeleteRange(buffer, startOff, endOff);
    } else if (y < Buffer_GetLineCount(buffer) - 1) {
        Buffer_JoinLine(buffer, y);
    }

    buffer->isModified = true;
    Tab_ClearSelection(tab);
    tab->isSaved = false;
}

void Editor_MoveLine(Editor* editor, int direction)
{
    if (Array_Size(&editor->tabs) == 0)
        return;
    Tab* tab = Array_Get(&editor->tabs, Tab*, editor->activeTabIndex);
    if (tab->buffer->isReadOnly)
        return;

    Buffer* buffer = tab->buffer;
    size_t lineCount = Buffer_GetLineCount(buffer);
    size_t y = tab->cursorY;

    if ((direction < 0 && y == 0) || (direction > 0 && y >= lineCount - 1))
        return;

    size_t first = (direction < 0) ? y - 1 : y;
    size_t second = first + 1;

    Line* lineA = Buffer_GetLine(buffer, first);
    Line* lineB = Buffer_GetLine(buffer, second);
    if (!lineA || !lineB)
        return;

    Slice textA = Line_GetText(lineA, buffer);
    char* aCopy = malloc(textA.size + 1);
    if (!aCopy)
        return;
    memcpy(aCopy, textA.data, textA.size);
    aCopy[textA.size] = '\0';

    Slice textB = Line_GetText(lineB, buffer);
    char* bCopy = malloc(textB.size + 1);
    if (!bCopy) {
        free(aCopy);
        return;
    }
    memcpy(bCopy, textB.data, textB.size);
    bCopy[textB.size] = '\0';

    size_t rangeStart = lineA->offset;
    size_t rangeEnd = lineB->offset + lineB->length;
    if (second < lineCount - 1)
        rangeEnd++;

    Buffer_RecordCompositeEdit(buffer, y, tab->cursorX);
    Buffer_DeleteRange(buffer, rangeStart, rangeEnd);

    size_t ip = rangeStart;
    if (textB.size > 0) {
        Buffer_InsertText(buffer, ip, bCopy, textB.size);
        ip += textB.size;
    }
    Buffer_InsertText(buffer, ip, "\n", 1);
    ip += 1;
    if (textA.size > 0) {
        Buffer_InsertText(buffer, ip, aCopy, textA.size);
        ip += textA.size;
    }
    if (second < lineCount - 1)
        Buffer_InsertText(buffer, ip, "\n", 1);

    free(aCopy);
    free(bCopy);

    buffer->isModified = true;

    tab->cursorY = (direction < 0) ? y - 1 : y + 1;
    Line* newRow = Buffer_GetLine(buffer, tab->cursorY);
    if (newRow && tab->cursorX > Line_Length(newRow))
        tab->cursorX = Line_Length(newRow);
    Tab_ClearSelection(tab); // the anchored region no longer describes this content
    tab->isSaved = false;
}

void Editor_InsertLineBelow(Editor* editor)
{
    if (Array_Size(&editor->tabs) == 0)
        return;
    Tab* tab = Array_Get(&editor->tabs, Tab*, editor->activeTabIndex);
    if (tab->buffer->isReadOnly)
        return;

    Buffer* buffer = tab->buffer;
    size_t y = tab->cursorY;
    Line* row = Buffer_GetLine(buffer, y);
    if (!row)
        return;

    size_t lineLen = Line_Length(row);
    Buffer_SplitLine(buffer, y, lineLen);

    tab->cursorY = y + 1;
    tab->cursorX = 0;
    Tab_ClearSelection(tab);
    tab->isSaved = false;
}

void Editor_InsertLineAbove(Editor* editor)
{
    if (Array_Size(&editor->tabs) == 0)
        return;
    Tab* tab = Array_Get(&editor->tabs, Tab*, editor->activeTabIndex);
    if (tab->buffer->isReadOnly)
        return;

    Buffer* buffer = tab->buffer;
    size_t y = tab->cursorY;

    Buffer_SplitLine(buffer, y, 0);

    tab->cursorY = y;
    tab->cursorX = 0;
    Tab_ClearSelection(tab);
    tab->isSaved = false;
}

void Editor_JoinLines(Editor* editor)
{
    if (Array_Size(&editor->tabs) == 0)
        return;
    Tab* tab = Array_Get(&editor->tabs, Tab*, editor->activeTabIndex);
    if (tab->buffer->isReadOnly)
        return;

    Buffer* buffer = tab->buffer;
    size_t y = tab->cursorY;

    if (y >= Buffer_GetLineCount(buffer) - 1)
        return;

    Line* row = Buffer_GetLine(buffer, y);
    size_t lineLen = row ? Line_Length(row) : 0;

    Buffer_JoinLine(buffer, y);

    tab->cursorX = lineLen;
    Tab_ClearSelection(tab);
    tab->isSaved = false;
}

void Editor_DeleteLine(Editor* editor)
{
    if (Array_Size(&editor->tabs) == 0)
        return;
    Tab* tab = Array_Get(&editor->tabs, Tab*, editor->activeTabIndex);
    if (tab->buffer->isReadOnly)
        return;

    Buffer* buffer = tab->buffer;
    size_t y = tab->cursorY;
    size_t lineCount = Buffer_GetLineCount(buffer);
    if (lineCount == 0)
        return;

    Buffer_RecordCompositeEdit(buffer, y, 0);
    Buffer_DeleteLine(buffer, y);

    if (y >= Buffer_GetLineCount(buffer) && Buffer_GetLineCount(buffer) > 0)
        tab->cursorY = Buffer_GetLineCount(buffer) - 1;
    tab->cursorX = 0;
    Tab_ClearSelection(tab);
    tab->isSaved = false;
}

void Editor_IndentLines(Editor* editor)
{
    if (Array_Size(&editor->tabs) == 0)
        return;
    Tab* tab = Array_Get(&editor->tabs, Tab*, editor->activeTabIndex);
    if (tab->buffer->isReadOnly)
        return;

    Buffer* buffer = tab->buffer;
    size_t startY, endY;

    if (tab->hasSelection) {
        size_t sx, sy, ex, ey;
        Tab_GetSelection(tab, &sx, &sy, &ex, &ey);
        startY = sy;
        endY = ey;
    } else {
        startY = tab->cursorY;
        endY = tab->cursorY;
    }

    Buffer_RecordCompositeEdit(buffer, startY, 0);
    for (size_t i = startY; i <= endY; i++) {
        Line* row = Buffer_GetLine(buffer, i);
        if (!row)
            continue;
        size_t offset = row->offset;
        // Build the indent string dynamically: inserting tabSize bytes from a
        // 4-byte literal read past it for tab_size > 4 (config clamps to
        // 1..16, and the cap here is defense in depth).
        char indent[16];
        size_t indentSize = tab->config->tabSize;
        if (indentSize > sizeof(indent))
            indentSize = sizeof(indent);
        memset(indent, ' ', indentSize);
        Buffer_InsertText(buffer, offset, indent, indentSize);
    }
    buffer->isModified = true;
    tab->isSaved = false;

    if (tab->hasSelection) {
        size_t ts = tab->config->tabSize;
        if (tab->selectStartY >= startY && tab->selectStartY <= endY)
            tab->selectStartX += ts;
        if (tab->cursorY >= startY && tab->cursorY <= endY)
            tab->cursorX += ts;
    }
}

void Editor_UnindentLines(Editor* editor)
{
    if (Array_Size(&editor->tabs) == 0)
        return;
    Tab* tab = Array_Get(&editor->tabs, Tab*, editor->activeTabIndex);
    if (tab->buffer->isReadOnly)
        return;

    Buffer* buffer = tab->buffer;
    size_t startY, endY;

    if (tab->hasSelection) {
        size_t sx, sy, ex, ey;
        Tab_GetSelection(tab, &sx, &sy, &ex, &ey);
        startY = sy;
        endY = ey;
    } else {
        startY = tab->cursorY;
        endY = tab->cursorY;
    }

    size_t rangeLen = endY - startY + 1;
    size_t* removals = malloc(rangeLen * sizeof(size_t));
    if (!removals)
        return;

    size_t totalRemoved = 0;
    for (size_t i = startY; i <= endY; i++) {
        Line* row = Buffer_GetLine(buffer, i);
        if (!row) {
            removals[i - startY] = 0;
            continue;
        }
        size_t spacesToRemove = 0;
        size_t tabSize = tab->config->tabSize;
        size_t lineLen = Line_Length(row);
        for (size_t j = 0; j < tabSize && j < lineLen; j++) {
            if (Line_GetChar(row, buffer, j) == ' ')
                spacesToRemove++;
            else
                break;
        }
        removals[i - startY] = spacesToRemove;
        totalRemoved += spacesToRemove;
    }

    if (totalRemoved == 0) {
        free(removals);
        return;
    }

    Buffer_RecordCompositeEdit(buffer, startY, 0);
    for (size_t i = startY; i <= endY; i++) {
        if (removals[i - startY] > 0) {
            Line* row = Buffer_GetLine(buffer, i);
            size_t offset = row->offset;
            Buffer_DeleteRange(buffer, offset, offset + removals[i - startY]);
        }
    }
    buffer->isModified = true;
    tab->isSaved = false;

    if (tab->hasSelection) {
        if (tab->selectStartY >= startY && tab->selectStartY <= endY) {
            size_t r = removals[tab->selectStartY - startY];
            tab->selectStartX = (tab->selectStartX >= r) ? tab->selectStartX - r : 0;
        }
        if (tab->cursorY >= startY && tab->cursorY <= endY) {
            size_t r = removals[tab->cursorY - startY];
            tab->cursorX = (tab->cursorX >= r) ? tab->cursorX - r : 0;
        }
    }
    free(removals);
}

char* Editor_Prompt(Editor* editor, const char* prompt)
{
    Array buf;
    Array_InitChar(&buf, 128);
    char nul = '\0';
    Array_Append(&buf, &nul, 1);

    while (1) {
        Editor_SetStatusMessage(editor, prompt, (const char*)buf.data);
        Editor_RefreshScreen(editor);

        int c = editor->platform->readKey(editor->platform->context);
        if (c == DELETE_KEY || c == CTRL_KEY('h') || c == BACKSPACE) {
            if (buf.size > 1)
                Array_ReplaceRange(&buf, buf.size - 2, 1, NULL, 0);
        } else if (c == '\x1b') {
            Editor_SetStatusMessage(editor, "");
            Array_Free(&buf);
            return NULL;
        } else if (c == '\r') {
            if (buf.size > 1) {
                Editor_SetStatusMessage(editor, "");
                char* result = strdup((const char*)buf.data);
                Array_Free(&buf);
                return result;
            }
        } else if (c >= 32 && c < 256 && c != 127) {
            char ch = (char)c;
            Array_ReplaceRange(&buf, buf.size - 1, 0, &ch, 1);
        }
    }
}

void Editor_ProcessKeypress(Editor* editor)
{
    if (Array_Size(&editor->tabs) == 0)
        return;

    Tab* activeTab = Array_Get(&editor->tabs, Tab*, editor->activeTabIndex);
    int input = editor->platform->readKey(editor->platform->context);
    // Any key other than the one whose warning is pending cancels the confirm.
    int previousConfirmKey = editor->pendingConfirmKey;
    if (input != previousConfirmKey)
        editor->pendingConfirmKey = 0;
    LOG_DEBUG("Editor received keypress: %d (char: '%c')", input, (input >= 32 && input < 127) ? (char)input : ' ');

    if (editor->isExplorerActive && input != RESIZE_EVENT && input != TERMINATE_EVENT) {
        Editor_ProcessExplorerInput(editor, input);
        return;
    }

    if (editor->isLogsActive && input != RESIZE_EVENT && input != TERMINATE_EVENT) {
        Editor_ProcessLogsInput(editor, input);
        return;
    }

    if (input == editor->config.keyQuit) {
        LOG_INFO("Quit key pressed. Exiting editor.");
        // Check if any tab is unsaved
        bool hasUnsaved = false;
        for (size_t i = 0; i < Array_Size(&editor->tabs); i++) {
            Tab* t = Array_Get(&editor->tabs, Tab*, i);
            if (!t->isSaved) {
                hasUnsaved = true;
                break;
            }
        }

        if (hasUnsaved && previousConfirmKey != input) {
            Editor_SetStatusMessage(editor, "Some files have unsaved changes! Press Quit key again to quit anyway.");
            editor->pendingConfirmKey = input;
            return;
        }
        exit(0);
    } else if (input == TERMINATE_EVENT) {
        // SIGINT/SIGTERM arrived: exit like a confirmed quit, from normal
        // (non-signal) context so atexit cleanup is safe to run.
        LOG_INFO("Termination signal received; exiting.");
        exit(0);
    } else if (input == RESIZE_EVENT) {
        Editor_UpdateGeometry(editor);
    } else if (input == editor->config.keyNewTab) {
        LOG_INFO("Creating new editor tab.");
        Editor_AddTab(editor, NULL);
    } else if (input == editor->config.keySaveAs) {
        char* filename = Editor_Prompt(editor, "Save as: %s");
        if (filename) {
            free(activeTab->filename);
            activeTab->filename = filename;
            Tab_SaveFile(activeTab);
            Editor_SetStatusMessage(editor, "Saved as %s", activeTab->filename);
        } else {
            Editor_SetStatusMessage(editor, "Save aborted.");
        }
    } else if (input == editor->config.keyExplorer) {
        LOG_INFO("Toggling explorer view.");
        editor->isExplorerActive = true;
        Editor_ReadDir(editor, ".");
    } else if (input == editor->config.keyLogs) {
        Editor_ToggleLogs(editor);
    } else if (input == editor->config.keyCloseTab) {
        LOG_INFO("Closing current active tab.");
        if (!activeTab->isSaved && previousConfirmKey != input) {
            Editor_SetStatusMessage(editor, "File has unsaved changes! Press Close Tab key again to close anyway.");
            editor->pendingConfirmKey = input;
            return;
        }
        editor->pendingConfirmKey = 0;
        Editor_CloseTab(editor);
    } else if (input == editor->config.keyNextTab) {
        if (Array_Size(&editor->tabs) > 0) {
            editor->activeTabIndex = (editor->activeTabIndex + 1) % Array_Size(&editor->tabs);
            LOG_INFO("Switched to next tab (index: %zu)", editor->activeTabIndex);
        }
    } else if (input == editor->config.keyPrevTab) {
        if (Array_Size(&editor->tabs) > 0) {
            if (editor->activeTabIndex == 0) {
                editor->activeTabIndex = Array_Size(&editor->tabs) - 1;
            } else {
                editor->activeTabIndex--;
            }
            LOG_INFO("Switched to previous tab (index: %zu)", editor->activeTabIndex);
        }
    } else {
        Editor_ProcessInput(editor, input);
    }
}

void Editor_ToggleFold(Editor* editor)
{
    if (Array_Size(&editor->tabs) == 0)
        return;
    Tab* tab = Array_Get(&editor->tabs, Tab*, editor->activeTabIndex);
    size_t lineIndex = tab->cursorY;
    Line* line = Buffer_GetLine(tab->buffer, lineIndex);
    if (!line)
        return;

    if (Line_IsFoldable(tab->buffer, lineIndex, tab->config->tabSize)) {
        bool wasFolded = line->isFolded;
        line->isFolded = !line->isFolded;
        if (line->isFolded && !wasFolded)
            tab->buffer->foldedLineCount++;
        else if (!line->isFolded && wasFolded)
            tab->buffer->foldedLineCount--;
        LOG_INFO("Toggled fold on line %zu to %d", lineIndex + 1, line->isFolded);
        Editor_ScrollTab(editor, tab);
    } else {
        Editor_SetStatusMessage(editor, "Line is not foldable (no indented block below it)");
    }
}

void Editor_ToggleAllFolds(Editor* editor)
{
    if (Array_Size(&editor->tabs) == 0)
        return;
    Tab* tab = Array_Get(&editor->tabs, Tab*, editor->activeTabIndex);
    size_t totalLines = Buffer_GetLineCount(tab->buffer);

    // Find if there is at least one unfolded foldable line
    bool anyUnfolded = false;
    for (size_t i = 0; i < totalLines; i++) {
        Line* line = Buffer_GetLine(tab->buffer, i);
        if (line && !line->isFolded && Line_IsFoldable(tab->buffer, i, tab->config->tabSize)) {
            anyUnfolded = true;
            break;
        }
    }

    // If there is any unfolded foldable line, fold all. Otherwise, unfold all.
    for (size_t i = 0; i < totalLines; i++) {
        Line* line = Buffer_GetLine(tab->buffer, i);
        if (line && Line_IsFoldable(tab->buffer, i, tab->config->tabSize)) {
            bool wasFolded = line->isFolded;
            line->isFolded = anyUnfolded;
            if (line->isFolded && !wasFolded)
                tab->buffer->foldedLineCount++;
            else if (!line->isFolded && wasFolded)
                tab->buffer->foldedLineCount--;
        }
    }

    LOG_INFO("Toggled all folds to %s", anyUnfolded ? "folded" : "unfolded");
    Editor_ScrollTab(editor, tab);
}
