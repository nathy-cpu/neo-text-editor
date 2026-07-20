#include "terminal.h"
#include "../utils/logger.h"
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

volatile sig_atomic_t windowResized = 0;

bool Terminal_EnableRawMode(Terminal* terminal)
{
    LOG_INFO("Enabling raw mode on terminal.");
    struct termios rawTermios;

    if (terminal->rawModeEnabled)
        return true;
    if (!isatty(STDIN_FILENO)) {
        LOG_ERROR("STDIN is not a TTY; raw mode cannot be enabled.");
        return false;
    }
    if (tcgetattr(STDIN_FILENO, &terminal->originalTermios) == -1) {
        LOG_ERROR("Failed to get terminal attributes (tcgetattr failed).");
        return false;
    }
    terminal->termiosSaved = true;

    rawTermios = terminal->originalTermios;
    rawTermios.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    rawTermios.c_oflag &= ~(OPOST);
    rawTermios.c_cflag |= (CS8);
    rawTermios.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    rawTermios.c_cc[VMIN] = 0;
    rawTermios.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &rawTermios) < 0) {
        LOG_ERROR("Failed to set terminal attributes (tcsetattr failed).");
        return false;
    }
    terminal->rawModeEnabled = true;

    // Enter alternate screen buffer and set cursor to blinking vertical bar
    const char* init = "\x1b[?1049h\x1b[5 q";
    write(STDOUT_FILENO, init, strlen(init));

    return true;
}

bool Terminal_DisableRawMode(Terminal* terminal)
{
    if (terminal->rawModeEnabled && terminal->termiosSaved) {
        LOG_INFO("Disabling raw mode on terminal.");
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &terminal->originalTermios);
        terminal->rawModeEnabled = false;

        // Reset cursor style to default, make sure cursor is visible, and exit alternate screen buffer
        const char* exitSeq = "\x1b[?25h\x1b[0 q\x1b[?1049l";
        write(STDOUT_FILENO, exitSeq, strlen(exitSeq));
    }
    return true;
}

bool Terminal_Restore(Terminal* terminal)
{
    Terminal_DisableRawMode(terminal);
    fflush(stdout);
    return true;
}

void Terminal_ClearScreen(const Terminal* terminal)
{
    (void)terminal;
    const char* clear = "\x1b[2J\x1b[H";
    write(STDOUT_FILENO, clear, strlen(clear));
}

int ReadKey(void)
{
    int readSize;
    char input;
    while ((readSize = read(STDIN_FILENO, &input, 1)) != 1) {
        if (readSize == -1) {
            if (errno == EINTR) {
                if (windowResized) {
                    windowResized = 0;
                    return RESIZE_EVENT;
                }
                continue;
            }
            if (errno != EAGAIN) {
                write(STDOUT_FILENO, "\x1b[2J", 4);
                write(STDOUT_FILENO, "\x1b[H", 3);
                perror("read");
                exit(1);
            }
        }
    }

    if (input == '\x1b') {
        char sequence[6];

        if (read(STDIN_FILENO, &sequence[0], 1) != 1)
            return '\x1b';

        if (sequence[0] == 's' || sequence[0] == 'S') {
            return ALT_S;
        }
        if (sequence[0] == 'f' || sequence[0] == 'F') {
            return ALT_F;
        }

        if (read(STDIN_FILENO, &sequence[1], 1) != 1)
            return '\x1b';

        if (sequence[0] == '[') {
            if (sequence[1] >= '0' && sequence[1] <= '9') {
                if (read(STDIN_FILENO, &sequence[2], 1) != 1)
                    return '\x1b';
                if (sequence[2] == '~') {
                    switch (sequence[1]) {
                    case '1':
                        return HOME_KEY;
                    case '3':
                        return DELETE_KEY;
                    case '4':
                        return END_KEY;
                    case '5':
                        return PAGE_UP;
                    case '6':
                        return PAGE_DOWN;
                    case '7':
                        return HOME_KEY;
                    case '8':
                        return END_KEY;
                    }
                } else if (sequence[2] == ';') {
                    if (read(STDIN_FILENO, &sequence[3], 1) != 1)
                        return '\x1b';
                    if (read(STDIN_FILENO, &sequence[4], 1) != 1)
                        return '\x1b';

                    if (sequence[3] == '5') {
                        switch (sequence[4]) {
                        case 'C':
                            return CTRL_ARROW_RIGHT;
                        case 'D':
                            return CTRL_ARROW_LEFT;
                        }
                    } else if (sequence[3] == '2') {
                        switch (sequence[4]) {
                        case 'A':
                            return SHIFT_ARROW_UP;
                        case 'B':
                            return SHIFT_ARROW_DOWN;
                        case 'C':
                            return SHIFT_ARROW_RIGHT;
                        case 'D':
                            return SHIFT_ARROW_LEFT;
                        }
                    }
                }
            } else {
                switch (sequence[1]) {
                case 'A':
                    return ARROW_UP;
                case 'B':
                    return ARROW_DOWN;
                case 'C':
                    return ARROW_RIGHT;
                case 'D':
                    return ARROW_LEFT;
                case 'H':
                    return HOME_KEY;
                case 'F':
                    return END_KEY;
                }
            }
        } else if (sequence[0] == 'O') {
            switch (sequence[1]) {
            case 'H':
                return HOME_KEY;
            case 'F':
                return END_KEY;
            }
        }

        return '\x1b';
    } else {
        return input;
    }
}

bool Terminal_GetCursorPosition(size_t* rows, size_t* columns)
{
    char buffer[32];
    unsigned int i = 0;

    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
        return false;

    while (i < sizeof(buffer) - 1) {
        if (read(STDIN_FILENO, &buffer[i], 1) != 1)
            break;
        if (buffer[i] == 'R')
            break;
        i++;
    }
    buffer[i] = '\0';

    if (buffer[0] != '\x1b' || buffer[1] != '[')
        return false;

    unsigned short int row, column;
    if (sscanf(&buffer[2], "%hu;%hu", &row, &column) != 2)
        return false;

    *rows = row;
    *columns = column;
    return true;
}

bool Terminal_GetWindowSize(size_t* rows, size_t* columns)
{
    struct winsize window;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &window) == -1 || window.ws_col == 0) {
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
            return false;
        return Terminal_GetCursorPosition(rows, columns);
    } else {
        *columns = window.ws_col;
        *rows = window.ws_row;
        return true;
    }
}

void Terminal_HandleSignal(Terminal* terminal, int signalNumber)
{
    if (terminal)
        Terminal_Restore(terminal);
    signal(signalNumber, SIG_DFL);
    raise(signalNumber);
}
