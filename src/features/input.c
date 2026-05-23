#include "../neo.h"
#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>

void Tab_MoveCursor(Tab* tab, int key)
{
    Line* row = Buffer_GetLine(tab->buffer, tab->cursorY);

    switch (key) {
    case ARROW_LEFT:
        if (tab->cursorX != 0)
            tab->cursorX--;
        else if (tab->cursorY > 0) {
            tab->cursorY--;
            Line* previousRow = Buffer_GetLine(tab->buffer, tab->cursorY);
            tab->cursorX = previousRow ? GapBuffer_Size(&previousRow->text) : 0;
        }
        break;
    case ARROW_RIGHT:
        if (row != NULL && tab->cursorX < GapBuffer_Size(&row->text))
            tab->cursorX++;
        else if (row != NULL && tab->cursorX == GapBuffer_Size(&row->text)) {
            if (tab->cursorY < Buffer_GetLineCount(tab->buffer) - 1) {
                tab->cursorY++;
                tab->cursorX = 0;
            }
        }
        break;
    case ARROW_UP:
        if (tab->cursorY != 0)
            tab->cursorY--;
        break;
    case ARROW_DOWN:
        if (tab->cursorY < Buffer_GetLineCount(tab->buffer) - 1)
            tab->cursorY++;
        break;
    }

    row = Buffer_GetLine(tab->buffer, tab->cursorY);
    size_t rowSize = (row != NULL) ? GapBuffer_Size(&row->text) : 0;
    if (tab->cursorX > rowSize)
        tab->cursorX = rowSize;
}

void Tab_ProcessKeypress(Tab* tab)
{
    static bool isQuiting = false;
    int input = ReadKey();
    bool modified = false;

    switch (input) {
    case '\r':
        Buffer_SplitLine(tab->buffer, tab->cursorY, tab->cursorX);
        tab->cursorY++;
        tab->cursorX = 0;
        tab->isSaved = false;
        modified = true;
        break;

    case CTRL_KEY('s'):
        Tab_SaveFile(tab);
        Editor_SetStatusMessage(tab->editor, "File saved.");
        break;

    case CTRL_KEY('q'):
        if (!tab->isSaved && !isQuiting) {
            Editor_SetStatusMessage(tab->editor, "File has unsaved changes! Press Ctrl-Q again to quit anyways.");
            isQuiting = true;
            return;
        }
        Terminal_ClearScreen((Terminal*)NULL);
        exit(0);
        break;

    case RESIZE_EVENT:
        Terminal_GetWindowSize(&tab->editor->screenRows, &tab->editor->screenColumns);
        if (tab->editor->screenRows > 2)
            tab->editor->screenRows -= 2;
        break;

    case HOME_KEY:
        tab->cursorX = 0;
        break;

    case END_KEY: {
        Line* row = Buffer_GetLine(tab->buffer, tab->cursorY);
        if (row)
            tab->cursorX = GapBuffer_Size(&row->text);
    } break;

    case BACKSPACE:
    case CTRL_KEY('h'):
    case DELETE_KEY:
        if (input == DELETE_KEY)
            Tab_MoveCursor(tab, ARROW_RIGHT);

        if (tab->cursorX > 0) {
            Buffer_DeleteChar(tab->buffer, tab->cursorY, tab->cursorX - 1);
            tab->cursorX--;
            tab->isSaved = false;
            modified = true;
        } else if (tab->cursorY > 0) {
            Line* previousRow = Buffer_GetLine(tab->buffer, tab->cursorY - 1);
            size_t previousLength = previousRow ? GapBuffer_Size(&previousRow->text) : 0;
            Buffer_JoinLine(tab->buffer, tab->cursorY - 1);
            tab->cursorY--;
            tab->cursorX = previousLength;
            tab->isSaved = false;
            modified = true;
        }
        break;

    case PAGE_UP:
    case PAGE_DOWN: {
        if (input == PAGE_UP)
            tab->cursorY = tab->rowOffset;
        else if (input == PAGE_DOWN) {
            tab->cursorY = tab->rowOffset + tab->editor->screenRows - 1;
            if (tab->cursorY > Buffer_GetLineCount(tab->buffer) - 1)
                tab->cursorY = Buffer_GetLineCount(tab->buffer) - 1;
        }

        for (size_t i = tab->editor->screenRows; i > 0; i--)
            Tab_MoveCursor(tab, input == PAGE_UP ? ARROW_UP : ARROW_DOWN);
    } break;

    case ARROW_UP:
    case ARROW_DOWN:
    case ARROW_LEFT:
    case ARROW_RIGHT:
        Tab_MoveCursor(tab, input);
        break;

    case CTRL_KEY('l'):
    case '\x1b':
        break;

    default:
        if (!iscntrl(input) && input < 128) {
            Buffer_InsertChar(tab->buffer, tab->cursorY, tab->cursorX, input);
            tab->cursorX++;
            tab->isSaved = false;
            modified = true;
        }
        break;
    }

    if (modified) {
        Tab_UpdateSyntax(tab);
    }

    isQuiting = false;
}
