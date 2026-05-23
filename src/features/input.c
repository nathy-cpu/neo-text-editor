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

void Tab_DeleteSelection(Tab* tab)
{
    if (!tab->hasSelection)
        return;
    size_t startX, startY, endX, endY;
    Tab_GetSelection(tab, &startX, &startY, &endX, &endY);

    tab->cursorX = endX;
    tab->cursorY = endY;

    while (tab->cursorY > startY || (tab->cursorY == startY && tab->cursorX > startX)) {
        if (tab->cursorX > 0) {
            Buffer_DeleteChar(tab->buffer, tab->cursorY, tab->cursorX - 1);
            tab->cursorX--;
        } else if (tab->cursorY > 0) {
            Line* previousRow = Buffer_GetLine(tab->buffer, tab->cursorY - 1);
            size_t previousLength = previousRow ? GapBuffer_Size(&previousRow->text) : 0;
            Buffer_JoinLine(tab->buffer, tab->cursorY - 1);
            tab->cursorY--;
            tab->cursorX = previousLength;
        }
    }
    tab->hasSelection = false;
    tab->isSaved = false;
}

static void Tab_MoveCursorWord(Tab* tab, int key)
{
    Line* row = Buffer_GetLine(tab->buffer, tab->cursorY);
    if (!row)
        return;

    if (key == CTRL_ARROW_RIGHT) {
        while (tab->cursorX < GapBuffer_Size(&row->text)) {
            char c = GapBuffer_Get(&row->text, tab->cursorX);
            if (isspace(c))
                break;
            tab->cursorX++;
        }
        while (tab->cursorX < GapBuffer_Size(&row->text)) {
            char c = GapBuffer_Get(&row->text, tab->cursorX);
            if (!isspace(c))
                break;
            tab->cursorX++;
        }
        if (tab->cursorX == GapBuffer_Size(&row->text) && tab->cursorY < Buffer_GetLineCount(tab->buffer) - 1) {
            tab->cursorY++;
            tab->cursorX = 0;
        }
    } else if (key == CTRL_ARROW_LEFT) {
        if (tab->cursorX == 0 && tab->cursorY > 0) {
            tab->cursorY--;
            row = Buffer_GetLine(tab->buffer, tab->cursorY);
            tab->cursorX = row ? GapBuffer_Size(&row->text) : 0;
            return;
        }
        while (tab->cursorX > 0) {
            char c = GapBuffer_Get(&row->text, tab->cursorX - 1);
            if (!isspace(c))
                break;
            tab->cursorX--;
        }
        while (tab->cursorX > 0) {
            char c = GapBuffer_Get(&row->text, tab->cursorX - 1);
            if (isspace(c))
                break;
            tab->cursorX--;
        }
    }
}

void Tab_ProcessInput(Tab* tab, int input)
{
    bool modified = false;

    switch (input) {
    case '\r':
        if (tab->hasSelection)
            Tab_DeleteSelection(tab);
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

    case CTRL_KEY('a'):
        tab->selectStartX = 0;
        tab->selectStartY = 0;
        tab->cursorY = Buffer_GetLineCount(tab->buffer) > 0 ? Buffer_GetLineCount(tab->buffer) - 1 : 0;
        Line* lastRow = Buffer_GetLine(tab->buffer, tab->cursorY);
        tab->cursorX = lastRow ? GapBuffer_Size(&lastRow->text) : 0;
        tab->hasSelection = true;
        break;

    case CTRL_KEY('q'):
        // Handled in App_ProcessKeypress
        break;

    case RESIZE_EVENT:
        // Handled in App_ProcessKeypress
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
        if (tab->hasSelection) {
            Tab_DeleteSelection(tab);
            modified = true;
        } else {
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
        tab->hasSelection = false;
        Tab_MoveCursor(tab, input);
        break;

    case CTRL_ARROW_LEFT:
    case CTRL_ARROW_RIGHT:
        tab->hasSelection = false;
        Tab_MoveCursorWord(tab, input);
        break;

    case SHIFT_ARROW_UP:
    case SHIFT_ARROW_DOWN:
    case SHIFT_ARROW_LEFT:
    case SHIFT_ARROW_RIGHT:
        if (!tab->hasSelection) {
            tab->hasSelection = true;
            tab->selectStartX = tab->cursorX;
            tab->selectStartY = tab->cursorY;
        }
        if (input == SHIFT_ARROW_UP)
            Tab_MoveCursor(tab, ARROW_UP);
        else if (input == SHIFT_ARROW_DOWN)
            Tab_MoveCursor(tab, ARROW_DOWN);
        else if (input == SHIFT_ARROW_LEFT)
            Tab_MoveCursor(tab, ARROW_LEFT);
        else if (input == SHIFT_ARROW_RIGHT)
            Tab_MoveCursor(tab, ARROW_RIGHT);
        break;

    case CTRL_KEY('l'):
    case '\x1b':
        break;

    default:
        if (!iscntrl(input) && input < 128) {
            if (tab->hasSelection) {
                Tab_DeleteSelection(tab);
            }
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
}

char* Editor_Prompt(App* app, const char* prompt)
{
    size_t bufsize = 128;
    char* buf = malloc(bufsize);
    size_t buflen = 0;
    buf[0] = '\0';

    while (1) {
        Tab* activeTab = Array_Get(&app->tabs, Tab*, app->activeTabIndex);
        Editor_SetStatusMessage(activeTab->editor, prompt, buf);
        App_RefreshScreen(app);

        int c = ReadKey();
        if (c == DELETE_KEY || c == CTRL_KEY('h') || c == BACKSPACE) {
            if (buflen != 0)
                buf[--buflen] = '\0';
        } else if (c == '\x1b') {
            Editor_SetStatusMessage(activeTab->editor, "");
            free(buf);
            return NULL;
        } else if (c == '\r') {
            if (buflen != 0) {
                Editor_SetStatusMessage(activeTab->editor, "");
                return buf;
            }
        } else if (!iscntrl(c) && c < 128) {
            if (buflen == bufsize - 1) {
                bufsize *= 2;
                buf = realloc(buf, bufsize);
            }
            buf[buflen++] = c;
            buf[buflen] = '\0';
        }
    }
}

void App_ProcessKeypress(App* app)
{
    if (Array_Size(&app->tabs) == 0)
        return;

    Tab* activeTab = Array_Get(&app->tabs, Tab*, app->activeTabIndex);
    static bool isQuiting = false;
    int input = ReadKey();

    if (app->isExplorerActive && input != RESIZE_EVENT) {
        Explorer_ProcessInput(app, input);
        return;
    }

    switch (input) {
    case CTRL_KEY('q'): {
        // Check if any tab is unsaved
        bool hasUnsaved = false;
        for (size_t i = 0; i < Array_Size(&app->tabs); i++) {
            Tab* t = Array_Get(&app->tabs, Tab*, i);
            if (!t->isSaved) {
                hasUnsaved = true;
                break;
            }
        }

        if (hasUnsaved && !isQuiting) {
            Editor_SetStatusMessage(
                activeTab->editor, "Some files have unsaved changes! Press Ctrl-Q again to quit anyways.");
            isQuiting = true;
            return;
        }
        Terminal_ClearScreen((Terminal*)NULL);
        exit(0);
    } break;

    case RESIZE_EVENT:
        App_UpdateGeometry(app);
        break;

    case CTRL_KEY('t'):
        App_AddTab(app, NULL); // New empty tab
        break;

    case ALT_S: {
        char* filename = Editor_Prompt(app, "Save as: %s");
        if (filename) {
            free(activeTab->filename);
            activeTab->filename = filename; // filename is already allocated by malloc
            Tab_SaveFile(activeTab);
            Editor_SetStatusMessage(activeTab->editor, "Saved as %s", activeTab->filename);
        } else {
            Editor_SetStatusMessage(activeTab->editor, "Save aborted.");
        }
    } break;

    case CTRL_KEY('e'): {
        app->isExplorerActive = true;
        // Start explorer in current directory, or tab's directory if we want.
        // For simplicity, just use "."
        Explorer_ReadDir(app, ".");
    } break;

    case CTRL_KEY('w'): {
        if (!activeTab->isSaved && !isQuiting) {
            Editor_SetStatusMessage(
                activeTab->editor, "File has unsaved changes! Press Ctrl-W again to close anyways.");
            isQuiting = true;
            return;
        }
        App_CloseTab(app);
    } break;

    case CTRL_KEY('n'):
        if (Array_Size(&app->tabs) > 0) {
            app->activeTabIndex = (app->activeTabIndex + 1) % Array_Size(&app->tabs);
        }
        break;

    case CTRL_KEY('p'):
        if (Array_Size(&app->tabs) > 0) {
            if (app->activeTabIndex == 0) {
                app->activeTabIndex = Array_Size(&app->tabs) - 1;
            } else {
                app->activeTabIndex--;
            }
        }
        break;

    case '\x1b': { // ESC sequence for Alt or other keys
        // We might need to read more chars if it's an escape sequence
        // This is a naive implementation since we read one key at a time, but ReadKey usually handles it.
        // Wait, ReadKey returns single keys or enums like ARROW_LEFT.
        // If it's literally ESC, pass to tab.
        Tab_ProcessInput(activeTab, input);
    } break;

    default:
        Tab_ProcessInput(activeTab, input);
        break;
    }

    if (input != CTRL_KEY('q') && input != CTRL_KEY('w')) {
        isQuiting = false;
    }
}
