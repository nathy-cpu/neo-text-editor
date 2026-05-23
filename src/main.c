#include "neo.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

static Terminal terminal = { 0 };

static void cleanupTerminal(void) { Terminal_Restore(&terminal); }

void signalHandler(int signalNumber)
{
    (void)signalNumber;
    exit(0); // atexit(cleanupTerminal) will fire
}

int main(int argc, char* argv[])
{
    // Set up signal handlers
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    // Initialize terminal
    if (!Terminal_EnableRawMode(&terminal)) {
        fprintf(stderr, "Failed to enable raw mode\n");
        return 1;
    }
    // Register cleanup to run on any exit() — covers Ctrl-Q, signals, and future paths
    atexit(cleanupTerminal);

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