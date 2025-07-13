#pragma once

#include <stdbool.h>
#include <termios.h>

// Encapsulates terminal state
typedef struct Terminal {
    struct termios originalTermios;
    bool rawModeEnabled;
    bool termiosSaved;
} Terminal;

// Enables raw mode. Returns 0 on success, -1 on failure.
int Terminal_EnableRawMode(Terminal *terminal);

// Disables raw mode. Returns 0 on success, -1 on failure.
int Terminal_DisableRawMode(Terminal *terminal);

// Restores the terminal to its original state. Safe to call multiple times.
int Terminal_Restore(Terminal *terminal);

// Clears the terminal screen using ANSI escape codes.
void Terminal_ClearScreen(Terminal *terminal);

// Reads a single key from the terminal. Raw mode must be enabled. Returns the character read, or -1 on error.
int ReadKey(void);

// Handles signals for terminal cleanup. Intended for use as a signal handler.
void Terminal_HandleSignal(Terminal *terminal, int signalNumber);