#define _POSIX_C_SOURCE 200809L
#include "tab.h"
#include "../utils/file_io.h"
#include "../utils/logger.h"
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
bool Tab_Init(Tab* tab)
{
    assert(tab != NULL);

    InitDefaultTestConfig();

    // Zero everything first so a failed init never leaves uninitialized
    // pointers (filename/visualRows) for Tab_Free or the renderer to trip on.
    memset(tab, 0, sizeof(Tab));

    // Initialize buffer
    tab->buffer = Buffer_New();
    if (!tab->buffer) {
        return false;
    }

    tab->syntax = NULL;
    tab->syntaxHighWaterMark = 0;

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
    tab->visualRowsLineCount = SIZE_MAX;

    // Selection state
    tab->hasSelection = false;
    tab->selectStartX = 0;
    tab->selectStartY = 0;

    tab->wrapLinesDisabledForSize = false;

    tab->config = &defaultTestConfig;
    tab->buffer->history.undoLimit = tab->config->undoLimit;

    return true;
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

    // The buffer changed, so nothing has been highlighted in it yet -- actual
    // highlighting is deferred to the render/scroll path, extended lazily as
    // the viewport moves, instead of covering the whole file up front here.
    tab->syntaxHighWaterMark = 0;
    Tab_SetSyntaxHighlight(tab);

    // Reset view state
    tab->cursorX = tab->cursorY = 0;
    tab->rowOffset = tab->columnOffset = 0;

    // The buffer pointer changed, so any cached visualRows/signature is stale
    tab->visualRowsEditVersion = SIZE_MAX;
    tab->visualRowsFoldedCount = SIZE_MAX;
    tab->visualRowsUsableColumns = SIZE_MAX;
    tab->visualRowsLineCount = SIZE_MAX;

    size_t lineCount = Buffer_GetLineCount(tab->buffer);
    tab->wrapLinesDisabledForSize
        = tab->config->wrapDisableLineThreshold > 0 && lineCount > tab->config->wrapDisableLineThreshold;
    if (tab->wrapLinesDisabledForSize) {
        LOG_INFO("Tab_LoadFile: word-wrap auto-disabled for '%s' (%zu lines exceeds threshold %zu)", path, lineCount,
            tab->config->wrapDisableLineThreshold);
    }

    LOG_INFO("Loaded tab content from file: %s (lines: %zu)", path, lineCount);
}

bool Tab_ShouldWrapLines(const Tab* tab)
{
    assert(tab && tab->config);
    return tab->config->wrapLines && !tab->wrapLinesDisabledForSize;
}

// Save tab content to file
void Tab_SaveFile(Tab* tab)
{
    if (!tab || !tab->filename) {
        LOG_ERROR("Cannot save tab: tab or filename is NULL");
        return;
    }

    bool useFsync = tab->config ? tab->config->fsyncEnabled : true;
    Buffer_OnSave(tab->buffer, tab->filename, useFsync);
    tab->isSaved = !tab->buffer->isModified;
}
