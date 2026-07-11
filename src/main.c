#define _POSIX_C_SOURCE 200809L
#include "neo.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Editor editor;

static void CleanupTerminal(void)
{
    Editor_RestoreTerminal(&editor);
    Editor_Free(&editor);
    Logger_Free();
}

void SignalHandler(int signalNumber)
{
    if (signalNumber == SIGWINCH) {
        windowResized = 1;
    } else {
        exit(0); // atexit(CleanupTerminal) will fire
    }
}

int main(int argc, char* argv[])
{
    // Set up signal handlers
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);

    // Initial default logger setup
    Logger_Init("neo.log", LOG_LEVEL_INFO, false, true, 1000);

    struct sigaction sa;
    sa.sa_handler = SignalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; // Explicitly NO SA_RESTART so read() is interrupted by SIGWINCH
    sigaction(SIGWINCH, &sa, NULL);

    // Parse CLI options
    CliOptions options;
    if (!CliOptions_Parse(&options, argc, argv)) {
        CliOptions_Free(&options);
        return 1;
    }

    if (options.helpRequested) {
        PrintHelp(argv[0]);
        CliOptions_Free(&options);
        return 0;
    }

    if (options.versionRequested) {
        PrintVersion();
        CliOptions_Free(&options);
        return 0;
    }

    // Initialize application
    Editor_Init(&editor);

    // Load configuration
    const char* configToLoad = options.configPath ? options.configPath : "config.lua";
    Editor_LoadConfig(&editor, configToLoad);

    // Apply CLI overrides
    if (options.overrideTabSize != -1) {
        editor.config.tabSize = options.overrideTabSize;
    }
    if (options.overrideShowLineNumbers != -1) {
        editor.config.showLineNumbers = (options.overrideShowLineNumbers == 1);
    }
    if (options.overrideWrapLines != -1) {
        editor.config.wrapLines = (options.overrideWrapLines == 1);
    }
    if (options.overrideSyntaxEnabled != -1) {
        editor.config.syntaxEnabled = (options.overrideSyntaxEnabled == 1);
    }

    if (options.overrideLogFile) {
        free(editor.config.logFile);
        editor.config.logFile = strdup(options.overrideLogFile);
    }
    if (options.overrideLogLevel) {
        free(editor.config.logLevelStr);
        editor.config.logLevelStr = strdup(options.overrideLogLevel);
    }
    if (options.overrideLogToFile != -1) {
        editor.config.logToFile = (options.overrideLogToFile == 1);
    }
    if (options.overrideLogToUi != -1) {
        editor.config.logToUi = (options.overrideLogToUi == 1);
    }
    if (options.overrideLogMaxMessages != -1) {
        editor.config.logMaxMessages = options.overrideLogMaxMessages;
    }

    // Reconfigure logger with final options
    LogLevel level = Logger_ParseLevel(editor.config.logLevelStr, LOG_LEVEL_INFO);
    Logger_Configure(
        editor.config.logFile, level, editor.config.logToFile, editor.config.logToUi, editor.config.logMaxMessages);

    LOG_INFO("Neo Text Editor starting up...");

    // Initialize terminal raw mode
    if (!Editor_InitTerminal(&editor)) {
        fprintf(stderr, "Failed to enable raw mode\n");
        CliOptions_Free(&options);
        return 1;
    }
    // Register cleanup to run on any exit() — covers Ctrl-Q, signals, and future paths
    atexit(CleanupTerminal);

    // Load files
    if (options.fileCount > 0) {
        for (int i = 0; i < options.fileCount; i++) {
            Editor_AddTab(&editor, options.files[i]);
            LOG_INFO("Opened file: %s", options.files[i]);
            Tab* tab = Array_Get(&editor.tabs, Tab*, i);
            if (options.readOnlyMode) {
                tab->buffer->isReadOnly = true;
            }

            // Jump to line and column if specified
            int line = options.fileLines[i];
            int col = options.fileColumns[i];
            if (line > 0) {
                size_t lineCount = Buffer_GetLineCount(tab->buffer);
                if ((size_t)line > lineCount) {
                    line = lineCount;
                }
                if (line > 0) {
                    tab->cursorY = line - 1;

                    // Set column
                    Line* lineObj = Buffer_GetLine(tab->buffer, tab->cursorY);
                    if (lineObj) {
                        size_t lineLen = Line_Length(lineObj);
                        if (col > 0) {
                            tab->cursorX = ((size_t)col <= lineLen) ? (size_t)(col - 1) : lineLen;
                        } else {
                            tab->cursorX = 0;
                        }
                    } else {
                        tab->cursorX = 0;
                    }
                }
            }
        }
    } else {
        Editor_AddTab(&editor, NULL);
        if (options.readOnlyMode) {
            Tab* tab = Array_Get(&editor.tabs, Tab*, 0);
            tab->buffer->isReadOnly = true;
        }
    }

    CliOptions_Free(&options);

    if (Array_Size(&editor.tabs) > 0 && editor.statusMessage[0] == '\0') {
        Editor_SetStatusMessage(&editor, "HELP: Configurable keybindings active. Press Ctrl-Q to quit.");
    }

    // Main event loop
    while (1) {
        if (Array_Size(&editor.tabs) > 0) {
            Editor_RefreshScreen(&editor);
        }
        Editor_ProcessKeypress(&editor);
    }

    return 0;
}
