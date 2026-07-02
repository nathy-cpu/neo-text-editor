#include "../neo.h"
#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>

void Editor_MoveCursor(Editor* editor, int key)
{
    if (Array_Size(&editor->tabs) == 0)
        return;
    Tab* tab = Array_Get(&editor->tabs, Tab*, editor->activeTabIndex);
    Line* row = Buffer_GetLine(tab->buffer, tab->cursorY);

    switch (key) {
    case ARROW_LEFT:
        if (tab->cursorX != 0)
            tab->cursorX--;
        else {
            size_t prevVisible = Tab_PrevVisibleLine(tab, tab->cursorY);
            if (prevVisible < tab->cursorY) {
                tab->cursorY = prevVisible;
                Line* previousRow = Buffer_GetLine(tab->buffer, tab->cursorY);
                tab->cursorX = previousRow ? GapBuffer_Size(&previousRow->text) : 0;
            }
        }
        break;
    case ARROW_RIGHT:
        if (row != NULL && tab->cursorX < GapBuffer_Size(&row->text))
            tab->cursorX++;
        else if (row != NULL && tab->cursorX == GapBuffer_Size(&row->text)) {
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
        if (currentVRowIdx + 1 < Array_Size(&tab->visualRows)) {
            size_t targetCol = Tab_GetCursorVisualCol(tab, currentVRowIdx);
            Tab_SetCursorFromVRow(tab, currentVRowIdx + 1, targetCol);
        }
        break;
    }
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

void Editor_DeleteSelection(Editor* editor)
{
    if (Array_Size(&editor->tabs) == 0)
        return;
    Tab* tab = Array_Get(&editor->tabs, Tab*, editor->activeTabIndex);
    if (tab->buffer->isReadOnly)
        return;
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

void Editor_MoveCursorWord(Editor* editor, int key)
{
    if (Array_Size(&editor->tabs) == 0)
        return;
    Tab* tab = Array_Get(&editor->tabs, Tab*, editor->activeTabIndex);
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

void Editor_ProcessInput(Editor* editor, int input)
{
    if (Array_Size(&editor->tabs) == 0)
        return;
    Tab* tab = Array_Get(&editor->tabs, Tab*, editor->activeTabIndex);
    bool modified = false;

    if (input == editor->config.keySave) {
        if (tab->buffer->isReadOnly) {
            Editor_SetStatusMessage(editor, "Error: File is read-only");
        } else {
            Tab_SaveFile(tab);
            Editor_SetStatusMessage(editor, "File saved.");
        }
    } else if (input == editor->config.keyToggleFold) {
        Editor_ToggleFold(editor);
    } else {
        switch (input) {
        case '\r':
            if (tab->buffer->isReadOnly) {
                Editor_SetStatusMessage(editor, "Error: File is read-only");
                break;
            }
            if (tab->hasSelection)
                Editor_DeleteSelection(editor);
            Buffer_SplitLine(tab->buffer, tab->cursorY, tab->cursorX);
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
            tab->cursorX = lastRow ? GapBuffer_Size(&lastRow->text) : 0;
            tab->hasSelection = true;
            break;

        case CTRL_KEY('q'):
            // Handled in Editor_ProcessKeypress
            break;

        case RESIZE_EVENT:
            // Handled in Editor_ProcessKeypress
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
            if (tab->buffer->isReadOnly) {
                Editor_SetStatusMessage(editor, "Error: File is read-only");
                break;
            }
            if (tab->hasSelection) {
                Editor_DeleteSelection(editor);
                modified = true;
            } else {
                if (input == DELETE_KEY)
                    Editor_MoveCursor(editor, ARROW_RIGHT);

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
                tab->cursorY = tab->rowOffset + editor->screenRows - 1;
                if (tab->cursorY > Buffer_GetLineCount(tab->buffer) - 1)
                    tab->cursorY = Buffer_GetLineCount(tab->buffer) - 1;
            }

            for (size_t i = editor->screenRows; i > 0; i--)
                Editor_MoveCursor(editor, input == PAGE_UP ? ARROW_UP : ARROW_DOWN);
        } break;

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
                Editor_MoveCursor(editor, ARROW_UP);
            else if (input == SHIFT_ARROW_DOWN)
                Editor_MoveCursor(editor, ARROW_DOWN);
            else if (input == SHIFT_ARROW_LEFT)
                Editor_MoveCursor(editor, ARROW_LEFT);
            else if (input == SHIFT_ARROW_RIGHT)
                Editor_MoveCursor(editor, ARROW_RIGHT);
            break;

        case CTRL_KEY('l'):
        case '\x1b':
            break;

        default:
            if (!iscntrl(input) && input < 128) {
                if (tab->buffer->isReadOnly) {
                    Editor_SetStatusMessage(editor, "Error: File is read-only");
                    break;
                }
                if (tab->hasSelection) {
                    Editor_DeleteSelection(editor);
                }
                Buffer_InsertChar(tab->buffer, tab->cursorY, tab->cursorX, input);
                tab->cursorX++;
                tab->isSaved = false;
                modified = true;
            }
            break;
        }
    }

    if (modified) {
        Tab_UpdateSyntax(tab);
    }
}

char* Editor_Prompt(Editor* editor, const char* prompt)
{
    size_t bufsize = 128;
    char* buf = malloc(bufsize);
    size_t buflen = 0;
    buf[0] = '\0';

    while (1) {
        Editor_SetStatusMessage(editor, prompt, buf);
        Editor_RefreshScreen(editor);

        int c = ReadKey();
        if (c == DELETE_KEY || c == CTRL_KEY('h') || c == BACKSPACE) {
            if (buflen != 0)
                buf[--buflen] = '\0';
        } else if (c == '\x1b') {
            Editor_SetStatusMessage(editor, "");
            free(buf);
            return NULL;
        } else if (c == '\r') {
            if (buflen != 0) {
                Editor_SetStatusMessage(editor, "");
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

void Editor_ProcessKeypress(Editor* editor)
{
    if (Array_Size(&editor->tabs) == 0)
        return;

    Tab* activeTab = Array_Get(&editor->tabs, Tab*, editor->activeTabIndex);
    static bool isQuiting = false;
    int input = ReadKey();
    LOG_DEBUG("Editor received keypress: %d (char: '%c')", input, (input >= 32 && input < 127) ? (char)input : ' ');

    if (editor->isExplorerActive && input != RESIZE_EVENT) {
        Editor_ProcessExplorerInput(editor, input);
        return;
    }

    if (editor->isLogsActive && input != RESIZE_EVENT) {
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

        if (hasUnsaved && !isQuiting) {
            Editor_SetStatusMessage(editor, "Some files have unsaved changes! Press Quit key again to quit anyway.");
            isQuiting = true;
            return;
        }
        Terminal_ClearScreen((Terminal*)NULL);
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
        if (!activeTab->isSaved && !isQuiting) {
            Editor_SetStatusMessage(editor, "File has unsaved changes! Press Close Tab key again to close anyway.");
            isQuiting = true;
            return;
        }
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

    if (input != editor->config.keyQuit && input != editor->config.keyCloseTab) {
        isQuiting = false;
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
        line->isFolded = !line->isFolded;
        LOG_INFO("Toggled fold on line %zu to %d", lineIndex + 1, line->isFolded);
        Editor_ScrollTab(editor, tab);
    } else {
        Editor_SetStatusMessage(editor, "Line is not foldable (no indented block below it)");
    }
}
