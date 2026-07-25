#pragma once

#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <termios.h>

#define CTRL_KEY(k) ((k) & 0x1f)
#define TAB_STOP 4

#define KEY_MOD_SHIFT 0x10000
#define KEY_MOD_ALT 0x20000
#define KEY_MOD_CTRL 0x40000

enum Key {
    BACKSPACE = 127,
    ARROW_LEFT = 2000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    DELETE_KEY,
    INSERT_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN,
    RESIZE_EVENT,
    KEY_F1,
    KEY_F2,
    KEY_F3,
    KEY_F4,
    KEY_F5,
    KEY_F6,
    KEY_F7,
    KEY_F8,
    KEY_F9,
    KEY_F10,
    KEY_F11,
    KEY_F12,
    TERMINATE_EVENT // SIGINT/SIGTERM arrived: main loop should exit cleanly
};

#define CTRL_ARROW_LEFT (ARROW_LEFT | KEY_MOD_CTRL)
#define CTRL_ARROW_RIGHT (ARROW_RIGHT | KEY_MOD_CTRL)
#define SHIFT_ARROW_UP (ARROW_UP | KEY_MOD_SHIFT)
#define SHIFT_ARROW_DOWN (ARROW_DOWN | KEY_MOD_SHIFT)
#define SHIFT_ARROW_LEFT (ARROW_LEFT | KEY_MOD_SHIFT)
#define SHIFT_ARROW_RIGHT (ARROW_RIGHT | KEY_MOD_SHIFT)
#define SHIFT_HOME_KEY (HOME_KEY | KEY_MOD_SHIFT)
#define SHIFT_END_KEY (END_KEY | KEY_MOD_SHIFT)
#define ALT_S ('s' | KEY_MOD_ALT)
#define ALT_F ('f' | KEY_MOD_ALT)

// Navigation compound keys
#define CTRL_HOME_KEY (HOME_KEY | KEY_MOD_CTRL)
#define CTRL_END_KEY (END_KEY | KEY_MOD_CTRL)
#define SHIFT_CTRL_HOME (HOME_KEY | KEY_MOD_SHIFT | KEY_MOD_CTRL)
#define SHIFT_CTRL_END (END_KEY | KEY_MOD_SHIFT | KEY_MOD_CTRL)
#define SHIFT_CTRL_LEFT (ARROW_LEFT | KEY_MOD_SHIFT | KEY_MOD_CTRL)
#define SHIFT_CTRL_RIGHT (ARROW_RIGHT | KEY_MOD_SHIFT | KEY_MOD_CTRL)
#define SHIFT_PAGE_UP (PAGE_UP | KEY_MOD_SHIFT)
#define SHIFT_PAGE_DOWN (PAGE_DOWN | KEY_MOD_SHIFT)
#define ALT_ARROW_UP (ARROW_UP | KEY_MOD_ALT)
#define ALT_ARROW_DOWN (ARROW_DOWN | KEY_MOD_ALT)
#define SHIFT_TAB ('\t' | KEY_MOD_SHIFT)

// Editing compound keys (sent via CSI u / modifyOtherKeys on modern terminals)
#define CTRL_BACKSPACE (BACKSPACE | KEY_MOD_CTRL)
#define CTRL_DELETE (DELETE_KEY | KEY_MOD_CTRL)
#define CTRL_ENTER ('\r' | KEY_MOD_CTRL)
#define CTRL_SHIFT_ENTER ('\r' | KEY_MOD_CTRL | KEY_MOD_SHIFT)
#define ALT_BACKSPACE (BACKSPACE | KEY_MOD_ALT)
#define ALT_DELETE (DELETE_KEY | KEY_MOD_ALT)

extern volatile sig_atomic_t windowResized;
extern volatile sig_atomic_t terminationRequested;

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
 * @brief Remembers the original termios for Terminal_AsyncRestore. Called by
 * Terminal_EnableRawMode.
 */
void Terminal_SetupAsyncRestore(const struct termios* originalTermios);

/**
 * @brief Async-signal-safe terminal restore: write()s the exit escape
 * sequence and tcsetattr()s the saved termios. Safe to call from fatal
 * signal handlers (no stdio, no allocation).
 */
void Terminal_AsyncRestore(void);
