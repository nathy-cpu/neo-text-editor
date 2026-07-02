#define _POSIX_C_SOURCE 200809L
#include "../neo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void Editor_ToggleLogs(Editor* editor)
{
    if (editor->isLogsActive) {
        editor->isLogsActive = false;
        LOG_INFO("Closed Log Viewer.");
    } else {
        editor->isLogsActive = true;
        editor->isExplorerActive = false;
        Array* logs = Logger_GetMessages();
        size_t numLogs = logs ? Array_Size(logs) : 0;
        editor->logsSelectedIndex = (numLogs > 0) ? numLogs - 1 : 0;
        LOG_INFO("Opened Log Viewer.");
    }
}

static void DrawHighlightedLogLine(Array* screenBuffer, const char* line, size_t maxCols, bool isSelected)
{
    size_t written = 0;
    if (isSelected) {
        Array_Append(screenBuffer, "\x1b[7m", 4);
    }

    size_t len = strlen(line);

    // Check for timestamp (first 19 characters, format "YYYY-MM-DD HH:MM:SS")
    bool hasTimestamp = (len >= 19 && line[4] == '-' && line[7] == '-' && line[10] == ' ' && line[13] == ':' && line[16] == ':');

    size_t currentIdx = 0;
    if (hasTimestamp) {
        if (!isSelected) {
            Array_Append(screenBuffer, "\x1b[90m", 5); // Dim gray
        }
        size_t printLen = (19 > maxCols) ? maxCols : 19;
        Array_Append(screenBuffer, line, printLen);
        written += printLen;
        if (!isSelected) {
            Array_Append(screenBuffer, "\x1b[m", 3);
        }
        currentIdx = 19;
    }

    // Scan rest of string
    while (currentIdx < len && written < maxCols) {
        if (line[currentIdx] == '[') {
            // Find closing bracket
            const char* closing = strchr(line + currentIdx, ']');
            if (closing) {
                size_t tokenLen = closing - (line + currentIdx) + 1;
                char token[128];
                if (tokenLen < sizeof(token)) {
                    strncpy(token, line + currentIdx, tokenLen);
                    token[tokenLen] = '\0';

                    bool isLevel = false;
                    const char* colorCode = NULL;
                    if (strcmp(token, "[DEBUG]") == 0) {
                        isLevel = true;
                        colorCode = "36"; // Cyan
                    } else if (strcmp(token, "[INFO]") == 0) {
                        isLevel = true;
                        colorCode = "32"; // Green
                    } else if (strcmp(token, "[WARN]") == 0 || strcmp(token, "[WARNING]") == 0) {
                        isLevel = true;
                        colorCode = "33"; // Yellow
                    } else if (strcmp(token, "[ERROR]") == 0) {
                        isLevel = true;
                        colorCode = "31"; // Red
                    } else if (strcmp(token, "[FATAL]") == 0) {
                        isLevel = true;
                        colorCode = "1;31"; // Bold Red
                    }

                    if (isLevel) {
                        if (!isSelected) {
                            char esc[16];
                            snprintf(esc, sizeof(esc), "\x1b[%sm", colorCode);
                            Array_Append(screenBuffer, esc, strlen(esc));
                        }
                        size_t printLen = (tokenLen > maxCols - written) ? maxCols - written : tokenLen;
                        Array_Append(screenBuffer, token, printLen);
                        written += printLen;
                        if (!isSelected) {
                            Array_Append(screenBuffer, "\x1b[m", 3);
                        }
                        currentIdx += tokenLen;
                        continue;
                    }

                    // Check if it's file:line format (contains ':')
                    bool isFileLine = (strchr(token, ':') != NULL);
                    if (isFileLine) {
                        if (!isSelected) {
                            Array_Append(screenBuffer, "\x1b[35m", 5); // Magenta
                        }
                        size_t printLen = (tokenLen > maxCols - written) ? maxCols - written : tokenLen;
                        Array_Append(screenBuffer, token, printLen);
                        written += printLen;
                        if (!isSelected) {
                            Array_Append(screenBuffer, "\x1b[m", 3);
                        }
                        currentIdx += tokenLen;
                        continue;
                    }
                }
            }
        }

        // Output character normally
        Array_Append(screenBuffer, &line[currentIdx], 1);
        written++;
        currentIdx++;
    }

    if (isSelected) {
        // Pad the remaining line width with spaces
        for (size_t p = written; p < maxCols; p++) {
            Array_Append(screenBuffer, " ", 1);
        }
        Array_Append(screenBuffer, "\x1b[m", 3);
    } else {
        Array_Append(screenBuffer, "\x1b[K", 3); // Clear line to end
    }
}

void Editor_DrawLogs(Editor* editor, Array* screenBuffer)
{
    size_t rows = 0;
    size_t cols = 0;
    Terminal_GetWindowSize(&rows, &cols);

    // Draw header
    Array_Append(screenBuffer, "\x1b[7m", 4);
    char header[256];
    int headerLen = snprintf(header, sizeof(header), " LOG VIEWER: (Press ESC or Ctrl-L to exit, Up/Down/PgUp/PgDn to scroll) ");
    if (headerLen > (int)cols)
        headerLen = cols;
    Array_Append(screenBuffer, header, headerLen);
    for (int i = headerLen; i < (int)cols; i++) {
        Array_Append(screenBuffer, " ", 1);
    }
    Array_Append(screenBuffer, "\x1b[m", 3);
    Array_Append(screenBuffer, "\r\n", 2);

    // Log items
    size_t displayRows = rows > 1 ? rows - 1 : 0;
    Array* logs = Logger_GetMessages();
    size_t numLogs = logs ? Array_Size(logs) : 0;
    size_t startIdx = 0;

    // Scroll selection logic
    if (editor->logsSelectedIndex >= displayRows) {
        startIdx = editor->logsSelectedIndex - displayRows + 1;
    }

    for (size_t i = 0; i < displayRows; i++) {
        size_t logIdx = startIdx + i;
        if (logIdx < numLogs) {
            char* logMsg = Array_Get(logs, char*, logIdx);
            bool isSelected = (logIdx == editor->logsSelectedIndex);
            DrawHighlightedLogLine(screenBuffer, logMsg, cols, isSelected);
        } else {
            Array_Append(screenBuffer, "~", 1);
            Array_Append(screenBuffer, "\x1b[K", 3);
        }

        if (i < displayRows - 1) {
            Array_Append(screenBuffer, "\r\n", 2);
        }
    }
}

void Editor_ProcessLogsInput(Editor* editor, int input)
{
    Array* logs = Logger_GetMessages();
    size_t numLogs = logs ? Array_Size(logs) : 0;

    switch (input) {
    case ARROW_UP:
        if (editor->logsSelectedIndex > 0) {
            editor->logsSelectedIndex--;
        }
        break;
    case ARROW_DOWN:
        if (numLogs > 0 && editor->logsSelectedIndex < numLogs - 1) {
            editor->logsSelectedIndex++;
        }
        break;
    case PAGE_UP: {
        size_t rows = 0;
        size_t cols = 0;
        Terminal_GetWindowSize(&rows, &cols);
        size_t displayRows = rows > 1 ? rows - 1 : 0;
        if (editor->logsSelectedIndex > displayRows) {
            editor->logsSelectedIndex -= displayRows;
        } else {
            editor->logsSelectedIndex = 0;
        }
        break;
    }
    case PAGE_DOWN: {
        size_t rows = 0;
        size_t cols = 0;
        Terminal_GetWindowSize(&rows, &cols);
        size_t displayRows = rows > 1 ? rows - 1 : 0;
        if (numLogs > 0) {
            if (editor->logsSelectedIndex + displayRows < numLogs) {
                editor->logsSelectedIndex += displayRows;
            } else {
                editor->logsSelectedIndex = numLogs - 1;
            }
        }
        break;
    }
    case '\x1b': // ESC
    case CTRL_KEY('q'):
        editor->isLogsActive = false;
        break;
    default:
        if (input == editor->config.keyLogs) {
            editor->isLogsActive = false;
        }
        break;
    }
}
