#pragma once

#include "../utils/array.h"
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    char* fileType;
    char** fileMatch;
    char** keywords;
    char** types;
    char* singleLineCommentStart;
    char* multiLineCommentStart;
    char* multiLineCommentEnd;
} Syntax;

typedef struct {
    int tabSize;
    bool showLineNumbers;
    bool wrapLines;
    // Auto-disables word-wrap for a file whose line count exceeds this, since
    // wrapping requires an eager full-document pass to materialize every
    // line's text and compute wrap segments. 0 disables this override
    // entirely (always respect wrapLines regardless of file size).
    size_t wrapDisableLineThreshold;
    bool syntaxEnabled;
    int statusTimeout;
    char* syntaxColors[9]; // Map of HighlightType enum values
    Array syntaxDatabase; // Dynamic Array of Syntax

    // Keybindings
    int keySave;
    int keyQuit;
    int keyNewTab;
    int keyCloseTab;
    int keyExplorer;
    int keyNextTab;
    int keyPrevTab;
    int keySaveAs;
    int keyLogs;
    int keyToggleFold;
    int keyToggleAllFolds;

    // Logging Configuration
    char* logFile;
    char* logLevelStr;
    bool logToFile;
    bool logToUi;
    int logMaxMessages;

    // Undo Configuration
    size_t undoLimit;

    // Fsync Configuration
    bool fsyncEnabled;
} Config;

struct Editor; // Forward declaration

/**
 * @brief Loads the Lua configuration file from disk.
 */
bool Editor_LoadConfig(struct Editor* editor, const char* configFilePath);

/**
 * @brief Initializes a Config object with default settings.
 */
void Config_InitDefaults(Config* config);

/**
 * @brief Frees all dynamic memory associated with a Config object.
 */
void Config_Free(Config* config);
