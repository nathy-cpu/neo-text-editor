#include "neo.h"
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

static Terminal terminal = {0};

void signal_handler(int sig) {
    (void)sig;
    Terminal_Restore(&terminal);
    exit(0);
}

int main(int argc, char* argv[]) {
    // Set up signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Initialize terminal
    if (Terminal_EnableRawMode(&terminal) != 0) {
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
    
    printf("Neo Text Editor - Press 'q' to quit\n");
    printf("File: %s\n", tab.filename ? tab.filename : "No file");
    printf("Content size: %zu bytes\n", GapBuffer_Size(&tab.text));
    
    // Simple input loop
    while (1) {
        int key = ReadKey();
        if (key == 'q') {
            break;
        }
        
        // Echo the key (for testing)
        printf("Key pressed: %c (%d)\n", key, key);
    }
    
    // Cleanup
    Tab_Free(&tab);
    Terminal_Restore(&terminal);
    
    return 0;
} 