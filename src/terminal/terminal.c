#include "terminal.h"
#include "../utils/logger.h"
#include "terminal_internal.h"
#include <ctype.h>
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
volatile sig_atomic_t terminationRequested = 0;

// Saved by Terminal_SetupAsyncRestore for async-signal-safe restoration.
static struct termios asyncRestoreTermios;
static volatile sig_atomic_t asyncRestoreArmed = 0;

void Terminal_SetupAsyncRestore(const struct termios* originalTermios)
{
    asyncRestoreTermios = *originalTermios;
    asyncRestoreArmed = 1;
}

void Terminal_AsyncRestore(void)
{
    // Only async-signal-safe calls: write() and tcsetattr() (POSIX AS-safe).
    if (!asyncRestoreArmed)
        return;
    static const char exitSequence[] = "\x1b[>4;0m\x1b[<1u\x1b[?25h\x1b[0 q\x1b[?1049l";
    ssize_t ignored = write(STDOUT_FILENO, exitSequence, sizeof(exitSequence) - 1);
    (void)ignored;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &asyncRestoreTermios);
}

// Best-effort terminal control write, retried on EINTR. Failure is
// deliberately swallowed: there is no meaningful recovery when the
// controlling terminal rejects an escape sequence, and fortified glibc
// (Ubuntu's default at -O1+) marks write() warn_unused_result, so the
// result must be consumed explicitly.
static void WriteTerminalSequence(const char* sequence, size_t size)
{
    size_t written = 0;
    while (written < size) {
        ssize_t result = write(STDOUT_FILENO, sequence + written, size - written);
        if (result < 0) {
            if (errno == EINTR)
                continue;
            return;
        }
        written += (size_t)result;
    }
}

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
    Terminal_SetupAsyncRestore(&terminal->originalTermios);

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

    // Enter alternate screen buffer, set cursor to blinking vertical bar, enable modifyOtherKeys level 2,
    // and request CSI u (Kitty protocol) for disambiguated key codes with modifiers
    const char* init = "\x1b[?1049h\x1b[5 q\x1b[>4;2m\x1b[>1u";
    WriteTerminalSequence(init, strlen(init));

    return true;
}

bool Terminal_DisableRawMode(Terminal* terminal)
{
    if (terminal->rawModeEnabled && terminal->termiosSaved) {
        LOG_INFO("Disabling raw mode on terminal.");
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &terminal->originalTermios);
        terminal->rawModeEnabled = false;

        // Disable modifyOtherKeys and Kitty CSI u, reset cursor style to default, make sure cursor is visible,
        // and exit alternate screen buffer
        const char* exitSeq = "\x1b[>4;0m\x1b[<1u\x1b[?25h\x1b[0 q\x1b[?1049l";
        WriteTerminalSequence(exitSeq, strlen(exitSeq));
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
    WriteTerminalSequence(clear, strlen(clear));
}

int TerminalGetModifierFlags(int pm)
{
    // Modifier parameters are 1+bitmask; 0 (empty/absent param) and 1 both
    // mean "no modifiers". Without this, pm=0 underflows to -1 = ALL flags.
    if (pm <= 1)
        return 0;
    int flags = 0;
    int val = pm - 1;
    if (val & 1)
        flags |= KEY_MOD_SHIFT;
    if (val & 2)
        flags |= KEY_MOD_ALT;
    if (val & 4)
        flags |= KEY_MOD_CTRL;
    return flags;
}

int TerminalMapCharCodeToKey(int char_code)
{
    // Kitty keyboard protocol functional keys (CSI u private-use codes).
    // Letter codes like 'H'/'F' must NOT appear here: they are character
    // codes in this context (CSI final-byte Home/End dispatch is separate),
    // and mapping them hijacked Shift+H / Alt+F as navigation keys.
    switch (char_code) {
    case 57414: // KP_ENTER
        return '\r';
    case 57417:
        return ARROW_LEFT;
    case 57418:
        return ARROW_RIGHT;
    case 57419:
        return ARROW_UP;
    case 57420:
        return ARROW_DOWN;
    case 57421:
        return PAGE_UP;
    case 57422:
        return PAGE_DOWN;
    case 57423:
        return HOME_KEY;
    case 57424:
        return END_KEY;
    case 57425:
        return INSERT_KEY;
    case 57426:
        return DELETE_KEY;
    default:
        // Lock keys (57358-57363), F13+ (57376+), and other unassigned
        // functional codes intentionally fall through unmapped; the input
        // layer ignores values that match no known key.
        return char_code;
    }
}

int TerminalMapModifiedKeyCode(int char_code, int mod_flags)
{
    if (mod_flags & KEY_MOD_CTRL) {
        if (char_code >= 'a' && char_code <= 'z') {
            return CTRL_KEY(char_code) | (mod_flags & ~KEY_MOD_CTRL);
        }
        if (char_code >= 'A' && char_code <= 'Z') {
            return CTRL_KEY(tolower((unsigned char)char_code)) | (mod_flags & ~KEY_MOD_CTRL);
        }
    }

    int mapped = TerminalMapCharCodeToKey(char_code);

    // Shift-only printable characters are TEXT, not chords: xterm
    // (modifyOtherKeys) reports the already-shifted keysym ('H', '!'),
    // kitty (CSI u) reports the base key ('h') plus the shift modifier.
    // Return the plain shifted character with NO flag -- `'H' | SHIFT` is
    // >= 0x10000 and gets dropped by the printable-input filter, making
    // capitals untypeable.
    if (mod_flags == KEY_MOD_SHIFT && mapped == char_code && char_code >= 32 && char_code != 127 && char_code < 57344) {
        if (char_code >= 'a' && char_code <= 'z')
            return toupper(char_code);
        return char_code;
    }

    return mapped | mod_flags;
}

// Reads one continuation byte of an escape sequence. Retries on EINTR so a
// signal mid-sequence (e.g. a resize while holding an arrow key) cannot abort
// the parse and leave the tail bytes to be typed into the document as text
// (the pending flag stays set; the NEXT ReadKey call reports the event).
// Returns 1 on byte, 0 on VTIME timeout/EOF/EAGAIN, -1 on hard error.
static int ReadByteRetry(char* out)
{
    for (;;) {
        ssize_t bytesRead = read(STDIN_FILENO, out, 1);
        if (bytesRead == 1)
            return 1;
        if (bytesRead == 0)
            return 0;
        if (errno == EINTR)
            continue;
        if (errno == EAGAIN)
            return 0;
        return -1;
    }
}

int ReadKey(void)
{
    int readSize;
    char input;
    for (;;) {
        // Check pending events on EVERY iteration -- not just after EINTR.
        // With VMIN=0/VTIME=1 the read wakes every 100ms, so a signal that
        // landed while we were NOT blocked in read() (e.g. during a redraw)
        // is still noticed promptly.
        if (terminationRequested) {
            terminationRequested = 0;
            return TERMINATE_EVENT;
        }
        if (windowResized) {
            windowResized = 0;
            return RESIZE_EVENT;
        }
        readSize = read(STDIN_FILENO, &input, 1);
        if (readSize == 1)
            break;
        if (readSize == -1 && errno != EINTR && errno != EAGAIN) {
            WriteTerminalSequence("\x1b[2J", 4);
            WriteTerminalSequence("\x1b[H", 3);
            perror("read");
            exit(1);
        }
        // readSize == 0 (VTIME timeout) or EINTR/EAGAIN: loop re-checks flags.
    }

    if (input == '\x1b') {
        char seq0;
        if (ReadByteRetry(&seq0) != 1) {
            return '\x1b';
        }

        if (seq0 == 'O') {
            char seq1;
            if (ReadByteRetry(&seq1) != 1) {
                return '\x1b';
            }
            switch (seq1) {
            case 'P':
                return KEY_F1;
            case 'Q':
                return KEY_F2;
            case 'R':
                return KEY_F3;
            case 'S':
                return KEY_F4;
            case 'H':
                return HOME_KEY;
            case 'F':
                return END_KEY;
            }
            return '\x1b';
        }

        if (seq0 == '[') {
            char seq[32];
            int seq_len = 0;
            char final_char = 0;

            while (seq_len < (int)sizeof(seq) - 1) {
                char c;
                if (ReadByteRetry(&c) != 1) {
                    return '\x1b';
                }
                seq[seq_len++] = c;
                if (c >= 0x40 && c <= 0x7E) {
                    final_char = c;
                    break;
                }
            }
            seq[seq_len] = '\0';

            if (final_char == 0) {
                return '\x1b';
            }

            int params[8] = { 0 };
            int num_params = 0;
            bool has_param = false;

            for (int i = 0; i < seq_len - 1; i++) {
                if (seq[i] >= '0' && seq[i] <= '9') {
                    // Clamp: unbounded accumulation is signed-overflow UB on
                    // long digit runs (e.g. pasted garbage). 65535 exceeds
                    // every assigned keycode/modifier, so clamped values just
                    // fail to match anything.
                    if (params[num_params] <= 6553) {
                        params[num_params] = params[num_params] * 10 + (seq[i] - '0');
                    } else {
                        params[num_params] = 65535;
                    }
                    has_param = true;
                } else if (seq[i] == ';' || seq[i] == ':') {
                    if (num_params < 7) {
                        num_params++;
                    }
                }
            }
            if (has_param) {
                num_params++;
            }

            if (final_char == '~') {
                if (num_params >= 1) {
                    if (params[0] == 27) {
                        // modifyOtherKeys format: CSI 27 ; <modifier> ; <char_code> ~
                        int mod_flags = 0;
                        if (num_params >= 2) {
                            mod_flags = TerminalGetModifierFlags(params[1]);
                        }
                        int char_code = (num_params >= 3) ? params[2] : 0;

                        return TerminalMapModifiedKeyCode(char_code, mod_flags);
                    } else {
                        int base_key = 0;
                        switch (params[0]) {
                        case 1:
                        case 7:
                            base_key = HOME_KEY;
                            break;
                        case 2:
                            base_key = INSERT_KEY;
                            break;
                        case 3:
                            base_key = DELETE_KEY;
                            break;
                        case 4:
                        case 8:
                            base_key = END_KEY;
                            break;
                        case 5:
                            base_key = PAGE_UP;
                            break;
                        case 6:
                            base_key = PAGE_DOWN;
                            break;
                        case 15:
                            base_key = KEY_F5;
                            break;
                        case 17:
                            base_key = KEY_F6;
                            break;
                        case 18:
                            base_key = KEY_F7;
                            break;
                        case 19:
                            base_key = KEY_F8;
                            break;
                        case 20:
                            base_key = KEY_F9;
                            break;
                        case 21:
                            base_key = KEY_F10;
                            break;
                        case 23:
                            base_key = KEY_F11;
                            break;
                        case 24:
                            base_key = KEY_F12;
                            break;
                        }
                        int mod_flags = 0;
                        if (num_params >= 2) {
                            mod_flags = TerminalGetModifierFlags(params[1]);
                        }
                        return base_key | mod_flags;
                    }
                }
            } else if (final_char == 'u') {
                // CSI u format (Kitty protocol): CSI <char_code> [; <modifier>] u
                // Modifier uses Kitty's 1+bitmask encoding: 2=Shift, 3=Alt, 5=Ctrl.
                if (num_params >= 1) {
                    int char_code = params[0];
                    int mod_flags = 0;
                    if (num_params >= 2) {
                        mod_flags = TerminalGetModifierFlags(params[1]);
                    }
                    LOG_DEBUG("CSI u: char_code=%d(0x%x) raw_mod=%d mod_flags=%d", char_code, char_code,
                        num_params >= 2 ? params[1] : 0, mod_flags);
                    int result = TerminalMapModifiedKeyCode(char_code, mod_flags);
                    LOG_DEBUG("CSI u result: %d", result);
                    return result;
                }
            } else if (final_char == 'A' || final_char == 'B' || final_char == 'C' || final_char == 'D') {
                int base_key = 0;
                switch (final_char) {
                case 'A':
                    base_key = ARROW_UP;
                    break;
                case 'B':
                    base_key = ARROW_DOWN;
                    break;
                case 'C':
                    base_key = ARROW_RIGHT;
                    break;
                case 'D':
                    base_key = ARROW_LEFT;
                    break;
                }
                int mod_flags = 0;
                if (num_params >= 2 && params[0] == 1) {
                    mod_flags = TerminalGetModifierFlags(params[1]);
                }
                return base_key | mod_flags;
            } else if (final_char == 'H' || final_char == 'F') {
                int base_key = (final_char == 'H') ? HOME_KEY : END_KEY;
                int mod_flags = 0;
                if (num_params >= 2 && params[0] == 1) {
                    mod_flags = TerminalGetModifierFlags(params[1]);
                }
                int result = base_key | mod_flags;
                LOG_DEBUG("CSI %c: raw_seq=\"%.*s\" num_params=%d params[0]=%d params[1]=%d mod_flags=%d result=%d",
                    final_char, seq_len, seq, num_params, num_params >= 1 ? params[0] : -1,
                    num_params >= 2 ? params[1] : -1, mod_flags, result);
                return result;
            } else if (final_char == 'Z') {
                // Shift+Tab
                return '\t' | KEY_MOD_SHIFT;
            } else if (final_char == 'P' || final_char == 'Q' || final_char == 'R' || final_char == 'S') {
                // F1 - F4 with modifiers: ESC[1;<modifier>P etc.
                int base_key = 0;
                switch (final_char) {
                case 'P':
                    base_key = KEY_F1;
                    break;
                case 'Q':
                    base_key = KEY_F2;
                    break;
                case 'R':
                    base_key = KEY_F3;
                    break;
                case 'S':
                    base_key = KEY_F4;
                    break;
                }
                int mod_flags = 0;
                if (num_params >= 2 && params[0] == 1) {
                    mod_flags = TerminalGetModifierFlags(params[1]);
                }
                return base_key | mod_flags;
            }

            return '\x1b';
        }

        // It is Alt + seq0
        int base = seq0;
        if (base >= 'A' && base <= 'Z') {
            base = tolower((unsigned char)base);
        }
        return base | KEY_MOD_ALT;
    } else {
        // Bytes >= 0x80 (UTF-8 leads/continuations) must come back as
        // positive values, not sign-extended negatives.
        return (unsigned char)input;
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
