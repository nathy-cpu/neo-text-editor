#include "../neo.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int CompareExplorerItems(const void* a, const void* b)
{
    const ExplorerItem* itemA = *(const ExplorerItem**)a;
    const ExplorerItem* itemB = *(const ExplorerItem**)b;

    if (itemA->isDir && !itemB->isDir)
        return -1;
    if (!itemA->isDir && itemB->isDir)
        return 1;

    return strcmp(itemA->name, itemB->name);
}

static void FormatMode(mode_t mode, char* buf)
{
    buf[0] = S_ISDIR(mode) ? 'd' : '-';
    buf[1] = (mode & S_IRUSR) ? 'r' : '-';
    buf[2] = (mode & S_IWUSR) ? 'w' : '-';
    buf[3] = (mode & S_IXUSR) ? 'x' : '-';
    buf[4] = (mode & S_IRGRP) ? 'r' : '-';
    buf[5] = (mode & S_IWGRP) ? 'w' : '-';
    buf[6] = (mode & S_IXGRP) ? 'x' : '-';
    buf[7] = (mode & S_IROTH) ? 'r' : '-';
    buf[8] = (mode & S_IWOTH) ? 'w' : '-';
    buf[9] = (mode & S_IXOTH) ? 'x' : '-';
    buf[10] = '\0';
}

static void FormatSize(off_t size, bool isDir, char* buf, size_t bufSize)
{
    if (isDir) {
        snprintf(buf, bufSize, "   - ");
        return;
    }
    const char* units[] = { "B", "K", "M", "G" };
    int unitIndex = 0;
    double dsize = size;
    while (dsize >= 1024.0 && unitIndex < 3) {
        dsize /= 1024.0;
        unitIndex++;
    }
    if (unitIndex == 0) {
        snprintf(buf, bufSize, "%4.0f%s", dsize, units[unitIndex]);
    } else {
        snprintf(buf, bufSize, "%4.1f%s", dsize, units[unitIndex]);
    }
}

void Explorer_ReadDir(App* app, const char* path)
{
    char* resolvedPath = realpath(path, NULL);
    if (!resolvedPath)
        return;

    DIR* dir = opendir(resolvedPath);
    if (!dir) {
        free(resolvedPath);
        return;
    }

    // Clear existing items
    for (size_t i = 0; i < Array_Size(&app->explorerItems); i++) {
        ExplorerItem* item = Array_Get(&app->explorerItems, ExplorerItem*, i);
        free(item->name);
        free(item);
    }
    Array_Clear(&app->explorerItems);

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0)
            continue;

        if (strcmp(entry->d_name, "..") == 0 && strcmp(resolvedPath, "/") == 0)
            continue; // Cannot go up from root

        char fullpath[1024];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", resolvedPath, entry->d_name);

        struct stat st;
        if (stat(fullpath, &st) == -1)
            continue;

        bool isDir = S_ISDIR(st.st_mode);

        char itemName[512];
        if (isDir) {
            snprintf(itemName, sizeof(itemName), "%s/", entry->d_name);
        } else {
            snprintf(itemName, sizeof(itemName), "%s", entry->d_name);
        }

        ExplorerItem* item = malloc(sizeof(ExplorerItem));
        item->name = strdup(itemName);
        item->isDir = isDir;
        item->mode = st.st_mode;
        item->size = st.st_size;
        item->mtime = st.st_mtime;

        Array_Append(&app->explorerItems, &item, 1);
    }
    closedir(dir);

    // Sort items
    qsort(app->explorerItems.data, Array_Size(&app->explorerItems), sizeof(ExplorerItem*), CompareExplorerItems);

    app->explorerSelectedIndex = 0;
    strncpy(app->currentExplorerPath, resolvedPath, sizeof(app->currentExplorerPath) - 1);
    app->currentExplorerPath[sizeof(app->currentExplorerPath) - 1] = '\0';
    free(resolvedPath);
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
    if (headerLen > (int)cols)
        headerLen = cols;
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

            ExplorerItem* item = Array_Get(&app->explorerItems, ExplorerItem*, itemIdx);

            char modeStr[16];
            FormatMode(item->mode, modeStr);

            char sizeStr[16];
            FormatSize(item->size, item->isDir, sizeStr, sizeof(sizeStr));

            char timeStr[32];
            struct tm* tmInfo = localtime(&item->mtime);
            strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M", tmInfo);

            char displayLine[1024];
            int len
                = snprintf(displayLine, sizeof(displayLine), " %s  %s  %s  %s", modeStr, sizeStr, timeStr, item->name);

            if (len > (int)cols)
                len = cols;
            Array_Append(screenBuffer, displayLine, len);

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
        if (numItems == 0)
            return;
        ExplorerItem* selectedItem = Array_Get(&app->explorerItems, ExplorerItem*, app->explorerSelectedIndex);
        char* selected = selectedItem->name;

        char newPath[1024];
        if (strcmp(selected, "../") == 0) {
            // Parent dir resolution on absolute path
            char* lastSlash = strrchr(app->currentExplorerPath, '/');
            if (lastSlash && lastSlash != app->currentExplorerPath) {
                *lastSlash = '\0';
                snprintf(newPath, sizeof(newPath), "%s", app->currentExplorerPath);
            } else {
                snprintf(newPath, sizeof(newPath), "/");
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
            newPath[strlen(newPath) - 1] = '\0';
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
