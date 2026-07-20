#pragma once

#include "../utils/array.h"
#include <stdbool.h>
#include <sys/types.h>
#include <time.h>

typedef struct {
    char* name;
    bool isDir;
    mode_t mode;
    off_t size;
    time_t mtime;
} ExplorerItem;

struct Editor; // Forward declaration

/**
 * @brief Reads a directory and populates the ExplorerItems list.
 */
void Editor_ReadDir(struct Editor* editor, const char* path);

/**
 * @brief Renders the File Explorer interface.
 */
void Editor_DrawExplorer(struct Editor* editor, Array* screenBuffer);

/**
 * @brief Processes a keypress while the File Explorer is active.
 */
void Editor_ProcessExplorerInput(struct Editor* editor, int input);
