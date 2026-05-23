#define _POSIX_C_SOURCE 200809L
#include "../neo.h"
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

// Initialize tab with default values
void Tab_Init(Tab* tab)
{
    assert(tab != NULL);

    // Initialize buffer
    tab->buffer = Buffer_New();
    if (!tab->buffer) {
        // Handle allocation failure
        return;
    }

    // Editor state
    tab->editor = malloc(sizeof(Editor));
    if (tab->editor) {
        Editor_Init(tab->editor);
    }

    tab->syntax = NULL;

    // Cursor state
    tab->cursorX = 0;
    tab->cursorY = 0;
    tab->renderX = 0;

    // Viewport state
    tab->rowOffset = 0;
    tab->columnOffset = 0;

    // File state
    tab->filename = NULL;
    tab->isSaved = true;
}

// Free all resources
void Tab_Free(Tab* tab)
{
    if (!tab)
        return;

    Buffer_Free(tab->buffer);
    if (tab->editor) {
        Editor_Free(tab->editor);
        free(tab->editor);
    }
    free(tab->filename); // Safe even if NULL
}

// Load file content into tab
void Tab_LoadFile(Tab* tab, const char* path)
{
    assert(tab && path);

    Array fileContent = { 0 };

    if (!FileIoRead(path, &fileContent)) {
        Array_Free(&fileContent);
        return; // Silent fail (caller can check filename)
    }

    // Clear existing content
    Buffer_Free(tab->buffer);
    tab->buffer = Buffer_New();

    // Parse file content into lines
    char* content = (char*)fileContent.data;
    size_t contentSize = fileContent.size;
    size_t lineStart = 0;
    size_t lineNumber = 0;

    for (size_t i = 0; i < contentSize; i++) {
        if (content[i] == '\n' || i == contentSize - 1) {
            size_t lineLength = i - lineStart;
            if (i == contentSize - 1 && content[i] != '\n') {
                lineLength++; // Include the last character if not a newline
            }

            if (lineLength > 0) {
                Line* line = Buffer_GetLine(tab->buffer, lineNumber);
                if (line) {
                    Line_InsertText(line, 0, content + lineStart, lineLength);
                }
            }

            if (content[i] == '\n') {
                lineNumber++;
                if (lineNumber >= Buffer_GetLineCount(tab->buffer)) {
                    Buffer_InsertLine(tab->buffer, lineNumber);
                }
            }

            lineStart = i + 1;
        }
    }

    // Update metadata
    free(tab->filename);
    tab->filename = strdup(path);
    tab->isSaved = true;

    Tab_SetSyntaxHighlight(tab);

    // Reset view state
    tab->cursorX = tab->cursorY = 0;
    tab->rowOffset = tab->columnOffset = 0;

    Array_Free(&fileContent);
}

// Save tab content to file
void Tab_SaveFile(Tab* tab)
{
    if (!tab || !tab->filename)
        return;

    Slice content = Buffer_ToSlice(tab->buffer);
    if (FileIoWrite(tab->filename, content)) {
        tab->isSaved = true;
    }
    free((void*)content.data);
}
