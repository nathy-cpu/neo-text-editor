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

    // Initialize text buffer with cache-line alignment
    GapBuffer_Init(&tab->text, 4096, 64); // 4KB initial, 64-byte aligned

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

    GapBuffer_Free(&tab->text);
    free(tab->filename); // Safe even if NULL
}

// Load file content into tab
void Tab_LoadFile(Tab* tab, const char* path)
{
    assert(tab && path);

    Array fileContent;
    Array_InitChar(&fileContent, 4096); // 4KB initial array

    if (!FileIO_Read(path, &fileContent)) {
        Array_Free(&fileContent);
        return; // Silent fail (caller can check filename)
    }

    // Replace content efficiently
    GapBuffer_Clear(&tab->text);
    GapBuffer_InsertSlice(&tab->text, 0,
        (Slice) { fileContent.data, fileContent.size });

    // Update metadata
    free(tab->filename);
    tab->filename = strdup(path);
    tab->isSaved = true;

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

    Slice content = GapBuffer_ToSlice(&tab->text);
    if (FileIO_Write(tab->filename, content)) {
        tab->isSaved = true;
    }
}
