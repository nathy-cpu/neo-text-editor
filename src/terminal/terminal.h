#pragma once

#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <termios.h>

#define CTRL_KEY(k) ((k) & 0x1f)
#define TAB_STOP 4

enum Key {
    BACKSPACE = 127,
    ARROW_LEFT = 2000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    DELETE_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN,
    RESIZE_EVENT,
    ALT_S,
    ALT_F,
    CTRL_ARROW_LEFT,
    CTRL_ARROW_RIGHT,
    SHIFT_ARROW_UP,
    SHIFT_ARROW_DOWN,
    SHIFT_ARROW_LEFT,
    SHIFT_ARROW_RIGHT
};

extern volatile sig_atomic_t windowResized;

// Encapsulates terminal state
typedef struct Terminal {
    struct termios originalTermios;
    bool rawModeEnabled;
    bool termiosSaved;
} Terminal;

/**
 * @brief Enables raw mode for the terminal.
 */
bool Terminal_EnableRawMode(Terminal* terminal);

/**
 * @brief Disables raw mode and restores original terminal settings.
 */
bool Terminal_DisableRawMode(Terminal* terminal);

/**
 * @brief Safely restores the terminal to its original state.
 */
bool Terminal_Restore(Terminal* terminal);

/**
 * @brief Clears the entire terminal screen using ANSI escape sequences.
 */
void Terminal_ClearScreen(const Terminal* terminal);

/**
 * @brief Reads a single keypress or escape sequence from the terminal.
 */
int ReadKey(void);

/**
 * @brief Queries the terminal for the current cursor position.
 */
bool Terminal_GetCursorPosition(size_t* rows, size_t* columns);

/**
 * @brief Retrieves the current dimensions of the terminal window.
 */
bool Terminal_GetWindowSize(size_t* rows, size_t* columns);

/**
 * @brief Signal handler callback for graceful terminal cleanup on exit/crash.
 */
void Terminal_HandleSignal(Terminal* terminal, int signalNumber);
