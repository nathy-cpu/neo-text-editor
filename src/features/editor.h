#pragma once

#include "../platform/platform.h"
#include "../terminal/terminal.h"
#include "../utils/array.h"
#include "config.h"
#include "tab.h"
#include <stddef.h>
#include <time.h>

// Editor - Main application state
typedef struct Editor {
    // Gateway to the outside world (terminal I/O, clipboard); tests inject fakes
    const Platform* platform;

    // Terminal state
    Terminal terminal;
    size_t screenRows;
    size_t screenColumns;

    // Status message state
    char statusMessage[200];
    time_t statusMessageTime;

    // Tabs state
    Array tabs; // Array of Tab*
    size_t activeTabIndex;

    // Explorer State
    bool isExplorerActive;
    Array explorerItems; // Array of dynamically allocated ExplorerItem pointers (ExplorerItem*)
    size_t explorerSelectedIndex;
    char currentExplorerPath[512];

    // Logs State
    bool isLogsActive;
    size_t logsSelectedIndex;

    // Destructive-action confirmation: the key whose warning is pending.
    // Confirmation fires only when the SAME key is pressed twice in a row --
    // quit and close-tab must not satisfy each other's warnings.
    int pendingConfirmKey;

    // Configuration
    Config config;
} Editor;

/**
 * @brief Initializes the global Editor application state.
 */
void Editor_Init(Editor* editor);

/**
 * @brief Frees the Editor application state and all of its tabs.
 */
void Editor_Free(Editor* editor);

/**
 * @brief Enables raw mode for the terminal.
 */
bool Editor_InitTerminal(Editor* editor);

/**
 * @brief Restores original terminal settings.
 */
void Editor_RestoreTerminal(Editor* editor);

/**
 * @brief Opens a new tab, optionally loading a file into it.
 */
void Editor_AddTab(Editor* editor, const char* filename);

/**
 * @brief Closes the currently active tab.
 */
void Editor_CloseTab(Editor* editor);

/**
 * @brief Recalculates screen dimensions and distributes them across tabs.
 */
void Editor_UpdateGeometry(Editor* editor);

/**
 * @brief Re-renders the entire terminal screen.
 */
void Editor_RefreshScreen(Editor* editor);

/**
 * @brief Sets a formatted status message to be displayed in the message bar.
 */
void Editor_SetStatusMessage(Editor* editor, const char* fstring, ...);

/**
 * @brief Renders the message bar at the bottom of the screen.
 */
void Editor_DrawMessageBar(Editor* editor, Array* screenBuffer);

/**
 * @brief Renders the visible rows of the active tab to the screen.
 */
void Editor_DrawTabRows(Editor* editor, Array* screenBuffer);

/**
 * @brief Renders the status bar (filename, line count, position) for the active tab.
 */
void Editor_DrawStatusBar(Editor* editor, Array* screenBuffer);

/**
 * @brief Prompts the user for input via the message bar.
 */
char* Editor_Prompt(Editor* editor, const char* prompt);

/**
 * @brief Calculates scroll offsets to ensure the cursor remains visible.
 */
void Editor_ScrollTab(Editor* editor, Tab* tab);

/**
 * @brief Toggles folding state for the current line.
 */
void Editor_ToggleFold(Editor* editor);

/**
 * @brief Toggles folding state for all foldable blocks in the file.
 */
void Editor_ToggleAllFolds(Editor* editor);
