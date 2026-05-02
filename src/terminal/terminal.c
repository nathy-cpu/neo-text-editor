#include "../neo.h"
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

int Terminal_EnableRawMode(Terminal* terminal)
{
    struct termios rawTermios;

    if (terminal->rawModeEnabled)
        return 0; // Already enabled
    if (!isatty(STDIN_FILENO))
        return -1;
    if (tcgetattr(STDIN_FILENO, &terminal->originalTermios) == -1)
        return -1;
    terminal->termiosSaved = true;

    rawTermios = terminal->originalTermios;
    rawTermios.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    rawTermios.c_oflag &= ~(OPOST);
    rawTermios.c_cflag |= (CS8);
    rawTermios.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    rawTermios.c_cc[VMIN] = 0;
    rawTermios.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &rawTermios) < 0)
        return -1;
    terminal->rawModeEnabled = true;
    return 0;
}

int Terminal_DisableRawMode(Terminal* terminal)
{
    if (terminal->rawModeEnabled && terminal->termiosSaved) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &terminal->originalTermios);
        terminal->rawModeEnabled = false;
    }
    return 0;
}

int Terminal_Restore(Terminal* terminal)
{
    Terminal_DisableRawMode(terminal);
    // Move cursor to home position and flush output
    const char* reset = "\r\n\x1b[H";
    write(STDOUT_FILENO, reset, strlen(reset));
    fflush(stdout);
    return 0;
}

void Terminal_ClearScreen(const Terminal* terminal)
{
    (void)terminal;
    const char* clear = "\x1b[2J\x1b[H";
    write(STDOUT_FILENO, clear, strlen(clear));
}

int ReadKey(void)
{
    char character;
    int numberRead;
    while ((numberRead = read(STDIN_FILENO, &character, 1)) == 0)
        ;
    if (numberRead == -1)
        return -1;
    return character;
}

void Terminal_HandleSignal(Terminal* terminal, int signalNumber)
{
    if (terminal)
        Terminal_Restore(terminal);
    signal(signalNumber, SIG_DFL);
    raise(signalNumber);
}
