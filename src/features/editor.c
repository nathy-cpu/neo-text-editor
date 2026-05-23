#include "../neo.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

void Editor_Init(Editor* editor)
{
    editor->screenRows = 0;
    editor->screenColumns = 0;
    editor->statusMessage[0] = '\0';
    editor->statusMessageTime = 0;
    TerminalGetWindowSize(&editor->screenRows, &editor->screenColumns);
    // Reserve 1 row for status bar (top) + 1 row for message bar (bottom)
    if (editor->screenRows > 2)
        editor->screenRows -= 2;
}

void Editor_Free(Editor* editor) { (void)editor; }

void Editor_SetStatusMessage(Editor* editor, const char* fstring, ...)
{
    va_list arg;
    va_start(arg, fstring);
    vsnprintf(editor->statusMessage, sizeof(editor->statusMessage), fstring, arg);
    va_end(arg);
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

void Tab_Scroll(Tab* tab)
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

    if (tab->cursorY >= tab->rowOffset + tab->editor->screenRows)
        tab->rowOffset = tab->cursorY - tab->editor->screenRows + 1;

    if (tab->renderX < tab->columnOffset)
        tab->columnOffset = tab->renderX;

    if (tab->renderX >= tab->columnOffset + tab->editor->screenColumns)
        tab->columnOffset = tab->renderX - tab->editor->screenColumns + 1;
}

void Tab_DrawRows(Tab* tab, Array* screenBuffer)
{
    size_t totalLines = Buffer_GetLineCount(tab->buffer);

    for (size_t i = 0; i < tab->editor->screenRows; i++) {
        size_t fileRow = i + tab->rowOffset;
        if (fileRow >= totalLines) {
            if (totalLines == 0 && i == tab->editor->screenRows / 3) {
                char welcome[50];
                int welcomelen = snprintf(welcome, sizeof(welcome), "Neo Text Editor");

                if (welcomelen > (int)tab->editor->screenColumns)
                    welcomelen = tab->editor->screenColumns;

                int padding = (tab->editor->screenColumns - welcomelen) / 2;
                if (padding > 0) {
                    Array_Append(screenBuffer, ">", 1);
                    padding--;
                }
                while (padding) {
                    Array_Append(screenBuffer, " ", 1);
                    padding--;
                }

                Array_Append(screenBuffer, welcome, welcomelen);
            } else
                Array_Append(screenBuffer, ">", 1);
        } else {
            Line* line = Buffer_GetLine(tab->buffer, fileRow);
            if (line) {
                // Ensure gaps are moved to the end so we can read contiguous data
                Slice textSlice = GapBuffer_ToSlice(&line->text);
                Slice styleSlice = GapBuffer_ToSlice(&line->styles);

                // GapBuffer_Size gives the actual logical size of the gap buffer
                size_t logicalSize = GapBuffer_Size(&line->text);

                // Handle column offset (horizontal scrolling)
                ssize_t len = logicalSize - tab->columnOffset;
                if (len < 0)
                    len = 0;
                if (len > (ssize_t)tab->editor->screenColumns)
                    len = tab->editor->screenColumns;

                if (len > 0) {
                    char* temp = (char*)textSlice.data + tab->columnOffset;
                    char* styles = (char*)styleSlice.data + tab->columnOffset;
                    int currentColor = -1;

                    for (ssize_t j = 0; j < len; j++) {
                        if (temp[j] == '\t') {
                            // Expand to next TAB_STOP boundary
                            size_t rx = Line_GetRenderX(line, tab->columnOffset + j);
                            int spaces = TAB_STOP - (rx % TAB_STOP);
                            while (spaces-- > 0)
                                Array_Append(screenBuffer, " ", 1);
                        } else if (iscntrl(temp[j])) {
                            char symbol = (temp[j] <= 26) ? '@' + temp[j] : '?';
                            Array_Append(screenBuffer, "\x1b[7m", 4);
                            Array_Append(screenBuffer, &symbol, 1);
                            Array_Append(screenBuffer, "\x1b[m", 3);
                            if (currentColor != -1) {
                                char cbuf[16];
                                int clen = snprintf(cbuf, sizeof(cbuf), "\x1b[%sm", GetSyntaxColor(currentColor));
                                Array_Append(screenBuffer, cbuf, clen);
                            }
                        } else {
                            int highlight = (j < (ssize_t)styleSlice.size - (ssize_t)tab->columnOffset)
                                ? styles[j]
                                : HIGHLIGHT_NORMAL;
                            if (highlight != currentColor) {
                                if (currentColor != -1)
                                    Array_Append(screenBuffer, "\x1b[39m", 5);
                                if (highlight != HIGHLIGHT_NORMAL) {
                                    char* color = GetSyntaxColor(highlight);
                                    char cbuf[16];
                                    int clen = snprintf(cbuf, sizeof(cbuf), "\x1b[%sm", color);
                                    Array_Append(screenBuffer, cbuf, clen);
                                }
                                currentColor = highlight;
                            }
                            Array_Append(screenBuffer, &temp[j], 1);
                        }
                    }
                    if (currentColor != -1 && currentColor != HIGHLIGHT_NORMAL) {
                        Array_Append(screenBuffer, "\x1b[39m", 5);
                    }
                }
            }
        }

        Array_Append(screenBuffer, "\x1b[K", 3);
        Array_Append(screenBuffer, "\r\n", 2);
    }
}

void Tab_DrawStatusBar(Tab* tab, Array* screenBuffer)
{
    Array_Append(screenBuffer, "\x1b[7m", 4);

    char* filename = (tab->filename == NULL) ? "[No File Opened]" : tab->filename;

    char status[140], cursor[50];

    char* saveStatus = tab->isSaved ? "" : "[UNSAVED]";
    int statusSize = snprintf(
        status, sizeof(status), "%s  %.50s ~ %zu lines", saveStatus, filename, Buffer_GetLineCount(tab->buffer));

    int cursorSize = snprintf(cursor, sizeof(cursor), "%zu:%zu", tab->cursorY + 1, tab->cursorX + 1);

    if (statusSize > (int)tab->editor->screenColumns)
        statusSize = tab->editor->screenColumns;

    Array_Append(screenBuffer, status, statusSize);

    for (int i = statusSize; i < (int)tab->editor->screenColumns; i++) {
        if (tab->editor->screenColumns - i == (size_t)cursorSize) {
            Array_Append(screenBuffer, cursor, cursorSize);
            break;
        } else
            Array_Append(screenBuffer, " ", 1);
    }

    Array_Append(screenBuffer, "\x1b[m", 3);
    Array_Append(screenBuffer, "\r\n", 2);
}

void Tab_RefreshScreen(Tab* tab)
{
    Tab_Scroll(tab);

    Array screenBuffer;
    Array_InitChar(&screenBuffer, 4096);

    // Hide cursor, move to top-left
    Array_Append(&screenBuffer, "\x1b[?25l", 6);
    Array_Append(&screenBuffer, "\x1b[H", 3);

    // 1. Status bar at the top
    Tab_DrawStatusBar(tab, &screenBuffer);

    // 2. Text rows (viewport)
    Tab_DrawRows(tab, &screenBuffer);

    // 3. Message bar at the bottom
    Editor_DrawMessageBar(tab->editor, &screenBuffer);

    // Position cursor: row 1 is status bar, so text starts at row 2
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "\x1b[%zu;%zuH",
        (tab->cursorY - tab->rowOffset) + 2,          // +2: row 1 = status bar
        (tab->renderX  - tab->columnOffset) + 1);

    Array_Append(&screenBuffer, buffer, strlen(buffer));
    Array_Append(&screenBuffer, "\x1b[?25h", 6);

    write(STDOUT_FILENO, screenBuffer.data, screenBuffer.size);
    Array_Free(&screenBuffer);
}
