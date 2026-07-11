#define _POSIX_C_SOURCE 200809L
#include "../neo.h"
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static Config defaultTestConfig;
static bool defaultTestConfigInitialized = false;

static void InitDefaultTestConfig(void)
{
    if (defaultTestConfigInitialized)
        return;
    defaultTestConfig.tabSize = 4;
    defaultTestConfig.showLineNumbers = true;
    defaultTestConfig.wrapLines = true;
    defaultTestConfig.syntaxEnabled = true;
    defaultTestConfig.statusTimeout = 5;
    for (int i = 0; i < 9; i++) {
        defaultTestConfig.syntaxColors[i] = NULL;
    }
    // Set database to empty array representation
    memset(&defaultTestConfig.syntaxDatabase, 0, sizeof(Array));
    defaultTestConfig.undoLimit = 1000;
    defaultTestConfigInitialized = true;
}

// Initialize tab with default values
void Tab_Init(Tab* tab)
{
    assert(tab != NULL);

    InitDefaultTestConfig();

    // Initialize buffer
    tab->buffer = Buffer_New();
    if (!tab->buffer) {
        // Handle allocation failure
        return;
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

    // Visual wrapping state
    Array_InitStruct(&tab->visualRows, VisualRow, 16);

    tab->config = &defaultTestConfig;
    tab->buffer->history.undoLimit = tab->config->undoLimit;
}

// Free all resources
void Tab_Free(Tab* tab)
{
    if (!tab)
        return;

    Buffer_Free(tab->buffer);
    free(tab->filename); // Safe even if NULL
    Array_Free(&tab->visualRows);
}

// Load file content into tab
void Tab_LoadFile(Tab* tab, const char* path)
{
    assert(tab && path);

    Array fileContent = { 0 };

    if (!FileIoRead(path, &fileContent)) {
        LOG_ERROR("Failed to load file into tab: %s", path);
        Array_Free(&fileContent);
        return; // Silent fail (caller can check filename)
    }

    // Clear existing content
    Buffer_Free(tab->buffer);
    tab->buffer = Buffer_New();
    tab->buffer->history.undoLimit = tab->config->undoLimit;

    // Parse file content into lines
    char* content = (char*)fileContent.data;
    size_t contentSize = fileContent.size;
    size_t lineStart = 0;
    size_t lineNumber = 0;
    Line* currentLine = tab->buffer->firstLine; // Initial empty line

    for (size_t i = 0; i < contentSize; i++) {
        if (content[i] == '\n' || i == contentSize - 1) {
            size_t lineLength = i - lineStart;
            if (i == contentSize - 1 && content[i] != '\n') {
                lineLength++; // Include the last character if not a newline
            }

            if (lineLength > 0 && currentLine) {
                Line_InsertText(currentLine, 0, content + lineStart, lineLength);
            }

            if (content[i] == '\n') {
                lineNumber++;
                currentLine = Buffer_InsertLine(tab->buffer, lineNumber);
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

    LOG_INFO("Loaded tab content from file: %s (lines: %zu)", path, Buffer_GetLineCount(tab->buffer));

    Array_Free(&fileContent);
}

// Save tab content to file
void Tab_SaveFile(Tab* tab)
{
    if (!tab || !tab->filename) {
        LOG_ERROR("Cannot save tab: tab or filename is NULL");
        return;
    }

    Slice content = Buffer_ToSlice(tab->buffer);
    if (FileIoWrite(tab->filename, content)) {
        tab->isSaved = true;
        LOG_INFO("Saved tab content to file: %s", tab->filename);
    } else {
        LOG_ERROR("Failed to save tab content to file: %s", tab->filename);
    }
    free((void*)content.data);
}
