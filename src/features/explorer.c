#include "../neo.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int CompareExplorerItems(const void* a, const void* b)
{
    const char* strA = *(const char**)a;
    const char* strB = *(const char**)b;

    // Sort directories first (they end with '/')
    bool isDirA = strA[strlen(strA) - 1] == '/';
    bool isDirB = strB[strlen(strB) - 1] == '/';

    if (isDirA && !isDirB) return -1;
    if (!isDirA && isDirB) return 1;

    return strcmp(strA, strB);
}

void Explorer_ReadDir(App* app, const char* path)
{
    DIR* dir = opendir(path);
    if (!dir) return;

    // Clear existing items
    for (size_t i = 0; i < Array_Size(&app->explorerItems); i++) {
        char* item = Array_Get(&app->explorerItems, char*, i);
        free(item);
    }
    Array_Clear(&app->explorerItems);

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0) continue;

        char fullpath[1024];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", path, entry->d_name);

        struct stat st;
        if (stat(fullpath, &st) == -1) continue;

        bool isDir = S_ISDIR(st.st_mode);

        char itemName[512];
        if (isDir) {
            snprintf(itemName, sizeof(itemName), "%s/", entry->d_name);
        } else {
            snprintf(itemName, sizeof(itemName), "%s", entry->d_name);
        }

        char* allocatedName = strdup(itemName);
        Array_Append(&app->explorerItems, &allocatedName, 1);
    }
    closedir(dir);

    // Sort items
    qsort(app->explorerItems.data, Array_Size(&app->explorerItems), sizeof(char*), CompareExplorerItems);

    app->explorerSelectedIndex = 0;
    strncpy(app->currentExplorerPath, path, sizeof(app->currentExplorerPath) - 1);
}

void Explorer_Draw(App* app, Array* screenBuffer)
{
    size_t rows = 0;
    size_t cols = 0;
    Terminal_GetWindowSize(&rows, &cols);

    // Header
    Array_Append(screenBuffer, "\x1b[7m", 4);
    char header[256];
    int headerLen = snprintf(header, sizeof(header), " EXPLORER: %s ", app->currentExplorerPath);
    if (headerLen > (int)cols) headerLen = cols;
    Array_Append(screenBuffer, header, headerLen);
    for (int i = headerLen; i < (int)cols; i++) {
        Array_Append(screenBuffer, " ", 1);
    }
    Array_Append(screenBuffer, "\x1b[m", 3);
    Array_Append(screenBuffer, "\r\n", 2);

    // Items
    size_t displayRows = rows > 1 ? rows - 1 : 0;
    size_t numItems = Array_Size(&app->explorerItems);
    size_t startIdx = 0;

    // Scroll logic
    if (app->explorerSelectedIndex >= displayRows) {
        startIdx = app->explorerSelectedIndex - displayRows + 1;
    }

    for (size_t i = 0; i < displayRows; i++) {
        size_t itemIdx = startIdx + i;
        if (itemIdx < numItems) {
            if (itemIdx == app->explorerSelectedIndex) {
                Array_Append(screenBuffer, "\x1b[7m", 4);
            }
            
            char* item = Array_Get(&app->explorerItems, char*, itemIdx);
            int len = strlen(item);
            if (len > (int)cols) len = cols;
            Array_Append(screenBuffer, item, len);

            if (itemIdx == app->explorerSelectedIndex) {
                for (int p = len; p < (int)cols; p++) {
                    Array_Append(screenBuffer, " ", 1);
                }
                Array_Append(screenBuffer, "\x1b[m", 3);
            } else {
                Array_Append(screenBuffer, "\x1b[K", 3);
            }
        } else {
            Array_Append(screenBuffer, "~", 1);
            Array_Append(screenBuffer, "\x1b[K", 3);
        }

        if (i < displayRows - 1) {
            Array_Append(screenBuffer, "\r\n", 2);
        }
    }
}

void Explorer_ProcessInput(App* app, int input)
{
    size_t numItems = Array_Size(&app->explorerItems);

    switch (input) {
    case ARROW_UP:
        if (app->explorerSelectedIndex > 0) {
            app->explorerSelectedIndex--;
        }
        break;
    case ARROW_DOWN:
        if (app->explorerSelectedIndex < numItems - 1) {
            app->explorerSelectedIndex++;
        }
        break;
    case '\x1b': // ESC
    case CTRL_KEY('e'):
    case CTRL_KEY('q'):
        app->isExplorerActive = false;
        break;
    case '\r': {
        if (numItems == 0) return;
        char* selected = Array_Get(&app->explorerItems, char*, app->explorerSelectedIndex);
        
        char newPath[1024];
        if (strcmp(selected, "../") == 0) {
            // Basic parent dir resolution
            char* lastSlash = strrchr(app->currentExplorerPath, '/');
            if (lastSlash && lastSlash != app->currentExplorerPath) {
                *lastSlash = '\0';
                snprintf(newPath, sizeof(newPath), "%s", app->currentExplorerPath);
            } else if (lastSlash == app->currentExplorerPath) {
                snprintf(newPath, sizeof(newPath), "/");
            } else {
                snprintf(newPath, sizeof(newPath), ".");
            }
            Explorer_ReadDir(app, newPath);
        } else if (selected[strlen(selected) - 1] == '/') {
            // It's a directory
            if (strcmp(app->currentExplorerPath, "/") == 0) {
                snprintf(newPath, sizeof(newPath), "/%s", selected);
            } else {
                snprintf(newPath, sizeof(newPath), "%s/%s", app->currentExplorerPath, selected);
            }
            // Remove trailing slash for ReadDir if needed, but opendir handles it
            newPath[strlen(newPath)-1] = '\0'; 
            Explorer_ReadDir(app, newPath);
        } else {
            // It's a file
            if (strcmp(app->currentExplorerPath, "/") == 0) {
                snprintf(newPath, sizeof(newPath), "/%s", selected);
            } else {
                snprintf(newPath, sizeof(newPath), "%s/%s", app->currentExplorerPath, selected);
            }
            Tab* activeTab = Array_Get(&app->tabs, Tab*, app->activeTabIndex);
            if (activeTab->filename == NULL && activeTab->isSaved) {
                Tab_LoadFile(activeTab, newPath);
            } else {
                App_AddTab(app, newPath);
            }
            app->isExplorerActive = false;
        }
        break;
    }
    }
}
