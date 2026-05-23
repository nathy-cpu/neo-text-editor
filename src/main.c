#include "neo.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

static Terminal terminal = { 0 };

void signalHandler(int signalNumber)
{
    (void)signalNumber;
    Terminal_Restore(&terminal);
    exit(0);
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