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

    // Initialize tab
    Tab tab;
    Tab_Init(&tab);

    // Load file if provided
    if (argc > 1) {
        Tab_LoadFile(&tab, argv[1]);
    }

    Editor_SetStatusMessage(tab.editor, "HELP: Ctrl-S = save | Ctrl-Q = quit");

    // Main event loop
    while (1) {
        Tab_RefreshScreen(&tab);
        Tab_ProcessKeypress(&tab);
    }

    // Cleanup
    Tab_Free(&tab);
    Terminal_Restore(&terminal);

    return 0;
}