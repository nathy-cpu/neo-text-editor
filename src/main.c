#include "neo.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

static Terminal terminal = { 0 };

static void CleanupTerminal(void) { Terminal_Restore(&terminal); }

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

    // Initialize terminal
    if (!Terminal_EnableRawMode(&terminal)) {
        fprintf(stderr, "Failed to enable raw mode\n");
        return 1;
    }
    // Register cleanup to run on any exit() — covers Ctrl-Q, signals, and future paths
    atexit(CleanupTerminal);

    // Initialize application
    App app;
    App_Init(&app);

    // Load files if provided
    if (argc > 1) {
        for (int i = 1; i < argc; i++) {
            App_AddTab(&app, argv[i]);
        }
    } else {
        App_AddTab(&app, NULL);
    }

    if (Array_Size(&app.tabs) > 0) {
        Tab* activeTab = Array_Get(&app.tabs, Tab*, app.activeTabIndex);
        Editor_SetStatusMessage(activeTab->editor, "HELP: Ctrl-S = save | Ctrl-Q = quit | Ctrl-W = close tab | Ctrl-N/P = switch tab");
    }

    // Main event loop
    while (1) {
        if (Array_Size(&app.tabs) > 0) {
            App_RefreshScreen(&app);
        }
        App_ProcessKeypress(&app);
    }

    // Cleanup
    App_Free(&app);
    Terminal_Restore(&terminal);

    return 0;
}
