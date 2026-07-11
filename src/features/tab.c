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
    tab->visualRowsEditVersion = SIZE_MAX;
    tab->visualRowsFoldedCount = SIZE_MAX;
    tab->visualRowsUsableColumns = SIZE_MAX;

    // Selection state
    tab->hasSelection = false;
    tab->selectStartX = 0;
    tab->selectStartY = 0;

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

    MappedFile mappedFile = FileIoMmap(path);
    if (mappedFile.fileDescriptor == -1) {
        LOG_INFO("File not found or failed to mmap, starting with empty buffer: %s", path);
        Buffer_Free(tab->buffer);
        tab->buffer = Buffer_New();
        if (tab->buffer) {
            tab->buffer->filename = strdup(path);
            tab->buffer->history.undoLimit = tab->config->undoLimit;
        }
    } else {
        Buffer_Free(tab->buffer);
        tab->buffer = Buffer_NewFromMmap(mappedFile, path);
        if (tab->buffer) {
            tab->buffer->history.undoLimit = tab->config->undoLimit;
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

    // The buffer pointer changed, so any cached visualRows/signature is stale
    tab->visualRowsEditVersion = SIZE_MAX;
    tab->visualRowsFoldedCount = SIZE_MAX;
    tab->visualRowsUsableColumns = SIZE_MAX;

    LOG_INFO("Loaded tab content from file: %s (lines: %zu)", path, Buffer_GetLineCount(tab->buffer));
}

// Save tab content to file
void Tab_SaveFile(Tab* tab)
{
    if (!tab || !tab->filename) {
        LOG_ERROR("Cannot save tab: tab or filename is NULL");
        return;
    }

    Buffer_OnSave(tab->buffer, tab->filename);
    tab->isSaved = !tab->buffer->isModified;
}
