#include "../neo.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void App_Init(App* app)
{
    assert(app != NULL);
    Array_Init(&app->tabs, sizeof(Tab*), 4, alignof(void*));
    app->activeTabIndex = 0;
}

void App_Free(App* app)
{
    if (!app)
        return;

    for (size_t i = 0; i < Array_Size(&app->tabs); i++) {
        Tab* tab = Array_Get(&app->tabs, Tab*, i);
        Tab_Free(tab);
        free(tab);
    }
    Array_Free(&app->tabs);
}

void App_AddTab(App* app, const char* filename)
{
    assert(app != NULL);

    Tab* newTab = malloc(sizeof(Tab));
    if (!newTab)
        return;

    Tab_Init(newTab);
    
    if (filename) {
        Tab_LoadFile(newTab, filename);
    }

    Array_Append(&app->tabs, &newTab, 1);
    app->activeTabIndex = Array_Size(&app->tabs) - 1;
    App_UpdateGeometry(app);
}

void App_CloseTab(App* app)
{
    assert(app != NULL);

    size_t numTabs = Array_Size(&app->tabs);
    if (numTabs == 0)
        return;

    Tab* activeTab = Array_Get(&app->tabs, Tab*, app->activeTabIndex);

    // Prompt logic or unsaved check should ideally happen before this is called
    // or we can handle it here if we want to block closing.
    // For now, just close it.
    
    Tab_Free(activeTab);
    free(activeTab);

    // Remove from array (shift remaining elements)
    for (size_t i = app->activeTabIndex; i < numTabs - 1; i++) {
        Tab* nextTab = Array_Get(&app->tabs, Tab*, i + 1);
        // Replace element at i with i+1
        // Since Array has no generic Insert/Remove yet, we just copy memory directly
        void* dest = Array_RawAt(&app->tabs, i);
        memcpy(dest, &nextTab, sizeof(Tab*));
    }
    
    app->tabs.size--;

    if (app->tabs.size == 0) {
        // No tabs left, quit application
        Terminal_ClearScreen((Terminal*)NULL);
        exit(0);
    }

    // Adjust active index
    if (app->activeTabIndex >= app->tabs.size) {
        app->activeTabIndex = app->tabs.size - 1;
    }
    App_UpdateGeometry(app);
}

void App_UpdateGeometry(App* app)
{
    size_t rows = 0;
    size_t cols = 0;
    if (!Terminal_GetWindowSize(&rows, &cols))
        return;

    size_t numTabs = Array_Size(&app->tabs);
    size_t reservedRows = (numTabs > 1) ? 3 : 2; // +1 for tabs bar if numTabs > 1

    for (size_t i = 0; i < numTabs; i++) {
        Tab* tab = Array_Get(&app->tabs, Tab*, i);
        tab->editor->screenColumns = cols;
        tab->editor->screenRows = (rows > reservedRows) ? (rows - reservedRows) : 0;
    }
}

static void App_DrawTabsBar(App* app, Array* screenBuffer)
{
    size_t numTabs = Array_Size(&app->tabs);
    if (numTabs <= 1)
        return;

    Tab* activeTab = Array_Get(&app->tabs, Tab*, app->activeTabIndex);
    size_t cols = activeTab->editor->screenColumns;
    if (cols == 0)
        return;

    size_t blockWidth = cols / numTabs;
    size_t extraCols = cols % numTabs;

    for (size_t i = 0; i < numTabs; i++) {
        Tab* tab = Array_Get(&app->tabs, Tab*, i);
        size_t currentBlockWidth = blockWidth + (i < extraCols ? 1 : 0);

        if (i == app->activeTabIndex) {
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

void App_RefreshScreen(App* app)
{
    size_t numTabs = Array_Size(&app->tabs);
    if (numTabs == 0)
        return;

    Tab* activeTab = Array_Get(&app->tabs, Tab*, app->activeTabIndex);
    Tab_Scroll(activeTab);

    Array screenBuffer;
    Array_InitChar(&screenBuffer, 4096);

    // Hide cursor, move to top-left
    Array_Append(&screenBuffer, "\x1b[?25l", 6);
    Array_Append(&screenBuffer, "\x1b[H", 3);

    // 1. Tabs bar
    if (numTabs > 1) {
        App_DrawTabsBar(app, &screenBuffer);
    }

    // 2. Status bar
    Tab_DrawStatusBar(activeTab, &screenBuffer);

    // 3. Text rows (viewport)
    Tab_DrawRows(activeTab, &screenBuffer);

    // 4. Message bar
    Editor_DrawMessageBar(activeTab->editor, &screenBuffer);

    // Position cursor
    size_t cursorRowOffset = (numTabs > 1) ? 3 : 2; // Row 1 or 2 is status bar, Tabs bar is Row 1 if >1 tabs
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "\x1b[%zu;%zuH",
        (activeTab->cursorY - activeTab->rowOffset) + cursorRowOffset,
        (activeTab->renderX - activeTab->columnOffset) + 1);

    Array_Append(&screenBuffer, buffer, strlen(buffer));
    Array_Append(&screenBuffer, "\x1b[?25h", 6);

    write(STDOUT_FILENO, screenBuffer.data, screenBuffer.size);
    Array_Free(&screenBuffer);
}
