#include "neo.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

static Editor editor;

static void CleanupTerminal(void)
{
    Editor_RestoreTerminal(&editor);
    Editor_Free(&editor);
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

    struct sigaction sa;
    sa.sa_handler = SignalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; // Explicitly NO SA_RESTART so read() is interrupted by SIGWINCH
    sigaction(SIGWINCH, &sa, NULL);

    // Initialize application
    Editor_Init(&editor);

    // Load configuration
    Editor_LoadConfig(&editor, "config.lua");

    // Initialize terminal raw mode
    if (!Editor_InitTerminal(&editor)) {
        fprintf(stderr, "Failed to enable raw mode\n");
        return 1;
    }
    // Register cleanup to run on any exit() — covers Ctrl-Q, signals, and future paths
    atexit(CleanupTerminal);

    // Load files if provided
    if (argc > 1) {
        for (int i = 1; i < argc; i++) {
            Editor_AddTab(&editor, argv[i]);
        }
    } else {
        Editor_AddTab(&editor, NULL);
    }

    if (Array_Size(&editor.tabs) > 0 && editor.statusMessage[0] == '\0') {
        Editor_SetStatusMessage(
            &editor, "HELP: Configurable keybindings active. Press Ctrl-Q to quit.");
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
