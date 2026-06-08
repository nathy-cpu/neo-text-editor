#include "../neo.h"
#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static bool IsSelected(Tab* tab, size_t row, size_t col)
{
    if (!tab->hasSelection)
        return false;
    size_t startX, startY, endX, endY;
    Tab_GetSelection(tab, &startX, &startY, &endX, &endY);

    if (row < startY || row > endY)
        return false;
    if (row == startY && col < startX)
        return false;
    if (row == endY && col >= endX)
        return false;

    return true;
}

void Editor_Init(Editor* editor)
{
    assert(editor != NULL);
    memset(editor, 0, sizeof(Editor));
    Array_Init(&editor->tabs, sizeof(Tab*), 4, alignof(void*));
    editor->activeTabIndex = 0;
    editor->isExplorerActive = false;
    Array_Init(&editor->explorerItems, sizeof(char*), 16, alignof(void*));
    editor->explorerSelectedIndex = 0;
    editor->currentExplorerPath[0] = '.';
    editor->currentExplorerPath[1] = '\0';
}

void Editor_Free(Editor* editor)
{
    if (!editor)
        return;

    for (size_t i = 0; i < Array_Size(&editor->tabs); i++) {
        Tab* tab = Array_Get(&editor->tabs, Tab*, i);
        Tab_Free(tab);
        free(tab);
    }
    Array_Free(&editor->tabs);

    for (size_t i = 0; i < Array_Size(&editor->explorerItems); i++) {
        char* item = Array_Get(&editor->explorerItems, char*, i);
        free(item);
    }
    Array_Free(&editor->explorerItems);
}

bool Editor_InitTerminal(Editor* editor)
{
    assert(editor != NULL);
    return Terminal_EnableRawMode(&editor->terminal);
}

void Editor_RestoreTerminal(Editor* editor)
{
    if (editor) {
        Terminal_Restore(&editor->terminal);
    }
}

void Editor_AddTab(Editor* editor, const char* filename)
{
    assert(editor != NULL);

    Tab* newTab = malloc(sizeof(Tab));
    if (!newTab)
        return;

    Tab_Init(newTab);

    if (filename) {
        Tab_LoadFile(newTab, filename);
    }

    Array_Append(&editor->tabs, &newTab, 1);
    editor->activeTabIndex = Array_Size(&editor->tabs) - 1;
    Editor_UpdateGeometry(editor);
}

void Editor_CloseTab(Editor* editor)
{
    assert(editor != NULL);

    size_t numTabs = Array_Size(&editor->tabs);
    if (numTabs == 0)
        return;

    Tab* activeTab = Array_Get(&editor->tabs, Tab*, editor->activeTabIndex);

    Tab_Free(activeTab);
    free(activeTab);

    // Remove from array (shift remaining elements)
    for (size_t i = editor->activeTabIndex; i < numTabs - 1; i++) {
        Tab* nextTab = Array_Get(&editor->tabs, Tab*, i + 1);
        void* dest = Array_RawAt(&editor->tabs, i);
        memcpy(dest, &nextTab, sizeof(Tab*));
    }

    editor->tabs.size--;

    if (editor->tabs.size == 0) {
        // No tabs left, quit application
        Editor_RestoreTerminal(editor);
        exit(0);
    }

    // Adjust active index
    if (editor->activeTabIndex >= editor->tabs.size) {
        editor->activeTabIndex = editor->tabs.size - 1;
    }
    Editor_UpdateGeometry(editor);
}

void Editor_UpdateGeometry(Editor* editor)
{
    size_t rows = 0;
    size_t cols = 0;
    if (!Terminal_GetWindowSize(&rows, &cols))
        return;

    editor->screenColumns = cols;

    size_t numTabs = Array_Size(&editor->tabs);
    size_t reservedRows = (numTabs > 1) ? 3 : 2; // +1 for tabs bar if numTabs > 1

    editor->screenRows = (rows > reservedRows) ? (rows - reservedRows) : 0;
}

void Editor_SetStatusMessage(Editor* editor, const char* formatString, ...)
{
    va_list arguments;
    va_start(arguments, formatString);
    vsnprintf(editor->statusMessage, sizeof(editor->statusMessage), formatString, arguments);
    va_end(arguments);
    editor->statusMessageTime = time(NULL);
}

void Editor_DrawMessageBar(Editor* editor, Array* screenBuffer)
{
    Array_Append(screenBuffer, "\x1b[7m", 4);

    int messageSize = strlen(editor->statusMessage);
    if (messageSize > (int)editor->screenColumns)
        messageSize = editor->screenColumns;

    if (messageSize > 0)
        Array_Append(screenBuffer, editor->statusMessage, messageSize);

    // Pad remaining columns with spaces to fill the full highlighted line
    for (int i = messageSize; i < (int)editor->screenColumns; i++)
        Array_Append(screenBuffer, " ", 1);

    Array_Append(screenBuffer, "\x1b[m", 3);
}

size_t Tab_GetGutterDigits(const Tab* tab)
{
    if (!tab || !tab->buffer)
        return 3;
    size_t totalLines = Buffer_GetLineCount(tab->buffer);
    if (totalLines == 0)
        totalLines = 1;
    size_t digits = 0;
    while (totalLines > 0) {
        digits++;
        totalLines /= 10;
    }
    return (digits < 3) ? 3 : digits;
}

size_t Tab_GetGutterWidth(const Tab* tab) { return Tab_GetGutterDigits(tab) + 3; }

void Editor_ScrollTab(Editor* editor, Tab* tab)
{
    // Compute visual render column from raw cursor byte-position
    tab->renderX = 0;
    if (tab->cursorY < Buffer_GetLineCount(tab->buffer)) {
        Line* line = Buffer_GetLine(tab->buffer, tab->cursorY);
        if (line)
            tab->renderX = Line_GetRenderX(line, tab->cursorX);
    }

    if (tab->cursorY < tab->rowOffset)
        tab->rowOffset = tab->cursorY;

    if (tab->cursorY >= tab->rowOffset + editor->screenRows)
        tab->rowOffset = tab->cursorY - editor->screenRows + 1;

    if (tab->renderX < tab->columnOffset)
        tab->columnOffset = tab->renderX;

    size_t gutterWidth = Tab_GetGutterWidth(tab);
    size_t usableColumns = (editor->screenColumns > gutterWidth) ? (editor->screenColumns - gutterWidth) : 0;

    if (usableColumns > 0) {
        if (tab->renderX >= tab->columnOffset + usableColumns)
            tab->columnOffset = tab->renderX - usableColumns + 1;
    } else {
        tab->columnOffset = 0;
    }
}

void Editor_DrawTabRows(Editor* editor, Array* screenBuffer)
{
    size_t numTabs = Array_Size(&editor->tabs);
    if (numTabs == 0)
        return;
    Tab* tab = Array_Get(&editor->tabs, Tab*, editor->activeTabIndex);
    size_t totalLines = Buffer_GetLineCount(tab->buffer);
    size_t gutterWidth = Tab_GetGutterWidth(tab);
    size_t digits = Tab_GetGutterDigits(tab);
    size_t usableColumns = (editor->screenColumns > gutterWidth) ? (editor->screenColumns - gutterWidth) : 0;

    for (size_t i = 0; i < editor->screenRows; i++) {
        size_t fileRow = i + tab->rowOffset;
        if (fileRow >= totalLines) {
            if (totalLines == 0 && i == editor->screenRows / 3) {
                char welcome[50];
                int welcomeLength = snprintf(welcome, sizeof(welcome), "Neo Text Editor");

                if (usableColumns > 0) {
                    Array_Append(screenBuffer, "\x1b[90m", 5);
                    for (size_t d = 0; d < gutterWidth; d++) {
                        Array_Append(screenBuffer, " ", 1);
                    }
                    Array_Append(screenBuffer, "\x1b[m", 3);
                }

                if (welcomeLength > (int)usableColumns)
                    welcomeLength = usableColumns;

                int padding = (usableColumns - welcomeLength) / 2;
                if (padding > 0) {
                    Array_Append(screenBuffer, "~", 1);
                    padding--;
                }
                while (padding) {
                    Array_Append(screenBuffer, " ", 1);
                    padding--;
                }

                Array_Append(screenBuffer, welcome, welcomeLength);
            } else {
                if (usableColumns > 0) {
                    Array_Append(screenBuffer, "\x1b[90m", 5);
                    for (size_t d = 0; d < gutterWidth; d++) {
                        Array_Append(screenBuffer, " ", 1);
                    }
                    Array_Append(screenBuffer, "~", 1);
                    Array_Append(screenBuffer, "\x1b[m", 3);
                } else {
                    Array_Append(screenBuffer, "~", 1);
                }
            }
        } else {
            Line* line = Buffer_GetLine(tab->buffer, fileRow);
            if (line) {
                // Ensure gaps are moved to the end so we can read contiguous data
                Slice textSlice = GapBuffer_ToSlice(&line->text);
                Slice styleSlice = GapBuffer_ToSlice(&line->styles);

                // GapBuffer_Size gives the actual logical size of the gap buffer
                size_t logicalSize = GapBuffer_Size(&line->text);

                // Draw styled gutter
                if (usableColumns > 0) {
                    char gutterBuf[32];
                    int gutterLen = snprintf(gutterBuf, sizeof(gutterBuf), " %*zu  ", (int)digits, fileRow + 1);
                    Array_Append(screenBuffer, "\x1b[90m", 5);
                    Array_Append(screenBuffer, gutterBuf, gutterLen);
                    Array_Append(screenBuffer, "\x1b[m", 3);
                }

                // Handle column offset (horizontal scrolling)
                ssize_t length = logicalSize - tab->columnOffset;
                if (length < 0)
                    length = 0;
                if (length > (ssize_t)usableColumns)
                    length = usableColumns;

                if (length > 0) {
                    char* textData = (char*)textSlice.data + tab->columnOffset;
                    char* styles = (char*)styleSlice.data + tab->columnOffset;
                    HighlightType currentColor = HIGHLIGHT_NORMAL;

                    for (ssize_t j = 0; j < length; j++) {
                        bool isSelected = IsSelected(tab, fileRow, tab->columnOffset + j);
                        if (isSelected)
                            Array_Append(screenBuffer, "\x1b[7m", 4);

                        if (textData[j] == '\t') {
                            // Expand to next TAB_STOP boundary
                            size_t renderX = Line_GetRenderX(line, tab->columnOffset + j);
                            int spaces = TAB_STOP - (renderX % TAB_STOP);
                            while (spaces-- > 0)
                                Array_Append(screenBuffer, " ", 1);
                        } else if (iscntrl(textData[j])) {
                            char symbol = (textData[j] <= 26) ? '@' + textData[j] : '?';
                            Array_Append(screenBuffer, "\x1b[7m", 4);
                            Array_Append(screenBuffer, &symbol, 1);
                            Array_Append(screenBuffer, "\x1b[27m", 5);
                            if (currentColor != HIGHLIGHT_NORMAL) {
                                char colorBuffer[16];
                                int colorLength = snprintf(
                                    colorBuffer, sizeof(colorBuffer), "\x1b[%sm", GetSyntaxColor(currentColor));
                                Array_Append(screenBuffer, colorBuffer, colorLength);
                            }
                        } else {
                            HighlightType highlight = (j < (ssize_t)styleSlice.size - (ssize_t)tab->columnOffset)
                                ? styles[j]
                                : HIGHLIGHT_NORMAL;
                            if (highlight != currentColor) {
                                if (currentColor != HIGHLIGHT_NORMAL)
                                    Array_Append(screenBuffer, "\x1b[39m", 5);
                                if (highlight != HIGHLIGHT_NORMAL) {
                                    char* color = GetSyntaxColor(highlight);
                                    char colorBuffer[16];
                                    int colorLength = snprintf(colorBuffer, sizeof(colorBuffer), "\x1b[%sm", color);
                                    Array_Append(screenBuffer, colorBuffer, colorLength);
                                }
                                currentColor = highlight;
                            }
                            Array_Append(screenBuffer, &textData[j], 1);
                        }

                        if (isSelected)
                            Array_Append(screenBuffer, "\x1b[27m", 5);
                    }
                    if (currentColor != HIGHLIGHT_NORMAL) {
                        Array_Append(screenBuffer, "\x1b[39m", 5);
                    }

                    if (length < (ssize_t)usableColumns && IsSelected(tab, fileRow, logicalSize)) {
                        Array_Append(screenBuffer, "\x1b[7m \x1b[27m", 10);
                    }
                } else if (IsSelected(tab, fileRow, logicalSize)) {
                    // For empty lines that are selected
                    if (usableColumns > 0) {
                        Array_Append(screenBuffer, "\x1b[7m \x1b[27m", 10);
                    }
                }
            }
        }

        Array_Append(screenBuffer, "\x1b[K", 3);
        Array_Append(screenBuffer, "\r\n", 2);
    }
}

void Editor_DrawStatusBar(Editor* editor, Array* screenBuffer)
{
    size_t numTabs = Array_Size(&editor->tabs);
    if (numTabs == 0)
        return;
    Tab* tab = Array_Get(&editor->tabs, Tab*, editor->activeTabIndex);

    Array_Append(screenBuffer, "\x1b[7m", 4);

    char* filename = (tab->filename == NULL) ? "[No File Opened]" : tab->filename;

    char status[140], cursor[50];

    char* saveStatus = tab->isSaved ? "" : "[UNSAVED]";
    int statusSize = snprintf(
        status, sizeof(status), "%s  %.50s ~ %zu lines", saveStatus, filename, Buffer_GetLineCount(tab->buffer));

    int cursorSize = snprintf(cursor, sizeof(cursor), "%zu:%zu", tab->cursorY + 1, tab->cursorX + 1);

    if (statusSize > (int)editor->screenColumns)
        statusSize = editor->screenColumns;

    Array_Append(screenBuffer, status, statusSize);

    for (int i = statusSize; i < (int)editor->screenColumns; i++) {
        if (editor->screenColumns - i == (size_t)cursorSize) {
            Array_Append(screenBuffer, cursor, cursorSize);
            break;
        } else
            Array_Append(screenBuffer, " ", 1);
    }

    Array_Append(screenBuffer, "\x1b[m", 3);
    Array_Append(screenBuffer, "\r\n", 2);
}

static void Editor_DrawTabsBar(Editor* editor, Array* screenBuffer)
{
    size_t numTabs = Array_Size(&editor->tabs);
    if (numTabs <= 1)
        return;

    size_t cols = editor->screenColumns;
    if (cols == 0)
        return;

    size_t blockWidth = cols / numTabs;
    size_t extraCols = cols % numTabs;

    for (size_t i = 0; i < numTabs; i++) {
        Tab* tab = Array_Get(&editor->tabs, Tab*, i);
        size_t currentBlockWidth = blockWidth + (i < extraCols ? 1 : 0);

        if (i == editor->activeTabIndex) {
            Array_Append(screenBuffer, "\x1b[7m", 4); // Highlight
        } else {
            Array_Append(screenBuffer, "\x1b[m", 3); // Normal
        }

        const char* name = tab->filename ? tab->filename : "[No Name]";
        size_t nameLen = strlen(name);
        char displayBuf[256];
        size_t displayLen = 0;

        if (nameLen <= currentBlockWidth) {
            // Fits entirely, render the full path centered or left aligned
            displayLen = nameLen;
            memcpy(displayBuf, name, displayLen);
        } else {
            // Check if basename fits
            const char* basename = strrchr(name, '/');
            basename = basename ? basename + 1 : name;
            size_t baseLen = strlen(basename);

            if (baseLen <= currentBlockWidth) {
                displayLen = baseLen;
                memcpy(displayBuf, basename, displayLen);
            } else {
                // Truncate basename
                displayLen = currentBlockWidth;
                memcpy(displayBuf, basename, displayLen);
                if (displayLen > 0) {
                    displayBuf[displayLen - 1] = '~';
                }
            }
        }

        // Write filename
        if (displayLen > 0) {
            // Let's just left align and pad with spaces for now
            Array_Append(screenBuffer, displayBuf, displayLen);
        }

        // Pad with spaces
        for (size_t p = displayLen; p < currentBlockWidth; p++) {
            Array_Append(screenBuffer, " ", 1);
        }
    }

    Array_Append(screenBuffer, "\x1b[m", 3); // Reset
    Array_Append(screenBuffer, "\r\n", 2);
}

void Editor_RefreshScreen(Editor* editor)
{
    size_t numTabs = Array_Size(&editor->tabs);
    if (numTabs == 0)
        return;

    Tab* activeTab = Array_Get(&editor->tabs, Tab*, editor->activeTabIndex);
    Editor_ScrollTab(editor, activeTab);

    Array screenBuffer;
    Array_InitChar(&screenBuffer, 4096);

    // Hide cursor, move to top-left
    Array_Append(&screenBuffer, "\x1b[?25l", 6);
    Array_Append(&screenBuffer, "\x1b[H", 3);

    if (editor->isExplorerActive) {
        Editor_DrawExplorer(editor, &screenBuffer);
    } else {
        // 1. Tabs bar
        if (numTabs > 1) {
            Editor_DrawTabsBar(editor, &screenBuffer);
        }

        // 2. Status bar
        Editor_DrawStatusBar(editor, &screenBuffer);

        // 3. Text rows (viewport)
        Editor_DrawTabRows(editor, &screenBuffer);

        // 4. Message bar
        Editor_DrawMessageBar(editor, &screenBuffer);

        // Position cursor
        size_t cursorRowOffset = (numTabs > 1) ? 3 : 2; // Row 1 or 2 is status bar, Tabs bar is Row 1 if >1 tabs
        size_t gutterWidth = Tab_GetGutterWidth(activeTab);
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "\x1b[%zu;%zuH", (activeTab->cursorY - activeTab->rowOffset) + cursorRowOffset,
            (activeTab->renderX - activeTab->columnOffset) + 1 + gutterWidth);

        Array_Append(&screenBuffer, buffer, strlen(buffer));
        Array_Append(&screenBuffer, "\x1b[?25h", 6);
    }

    write(STDOUT_FILENO, screenBuffer.data, screenBuffer.size);
    Array_Free(&screenBuffer);
}
