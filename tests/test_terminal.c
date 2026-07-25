#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "../src/terminal/terminal.h"

// This test binary's stdin is not attached to a real TTY (make test pipes/
// redirects it), so Terminal_EnableRawMode is expected to fail via its isatty
// guard here. These tests assert the behavior that actually matches the
// current environment instead of only printing PASS/FAIL without checking it,
// while still asserting the correct (opposite) behavior if ever run with a
// real terminal attached to stdin.

void test_Terminal_EnableRawMode(void) {
    Terminal term = {0};
    bool result = Terminal_EnableRawMode(&term);

    if (isatty(STDIN_FILENO)) {
        assert(result == true);
        assert(term.rawModeEnabled == true);
    } else {
        assert(result == false);
        assert(term.rawModeEnabled == false);
    }

    Terminal_Restore(&term);
}

void test_Terminal_EnableRawMode_idempotent(void) {
    // Enabling raw mode twice in a row must short-circuit on the second call
    // (rawModeEnabled already true) rather than re-doing the tcsetattr dance.
    if (!isatty(STDIN_FILENO))
        return; // Nothing to exercise without a real TTY to actually enable.

    Terminal term = {0};
    assert(Terminal_EnableRawMode(&term) == true);
    assert(Terminal_EnableRawMode(&term) == true);
    assert(term.rawModeEnabled == true);

    Terminal_Restore(&term);
}

void test_Terminal_DisableRawMode(void) {
    Terminal term = {0};
    Terminal_EnableRawMode(&term);
    bool result = Terminal_DisableRawMode(&term);
    assert(result == true);
    assert(term.rawModeEnabled == false);
}

void test_Terminal_DisableRawMode_never_enabled(void) {
    // Disabling raw mode that was never enabled must be a safe no-op, not a
    // crash from touching an unset originalTermios.
    Terminal term = {0};
    bool result = Terminal_DisableRawMode(&term);
    assert(result == true);
    assert(term.rawModeEnabled == false);
}

void test_Terminal_Restore(void) {
    Terminal term = {0};
    Terminal_EnableRawMode(&term);
    bool result = Terminal_Restore(&term);
    assert(result == true);
    assert(term.rawModeEnabled == false);
}

void test_Terminal_Restore_never_enabled(void) {
    // Restoring a terminal that was never put into raw mode must also be safe.
    Terminal term = {0};
    bool result = Terminal_Restore(&term);
    assert(result == true);
    assert(term.rawModeEnabled == false);
}

void test_Terminal_ClearScreen(void) {
    // No way to assert screen contents from here; this only confirms the call
    // completes without crashing.
    Terminal term = {0};
    Terminal_ClearScreen(&term);
}

// Redirects STDIN_FILENO to a pipe pre-loaded with `bytes`, calls ReadKey(),
// then restores the original stdin. `bytes` must represent one complete key
// sequence understood by ReadKey -- once it's fully resolved, ReadKey returns
// without attempting to read further, so closing the pipe's write end (giving
// EOF for anything beyond what we supplied) cannot cause it to block.
static int ReadKeyFromBytes(const char* bytes, size_t len) {
    int pipefd[2];
    assert(pipe(pipefd) == 0);
    ssize_t written = write(pipefd[1], bytes, len);
    assert(written == (ssize_t)len);
    close(pipefd[1]);

    int savedStdin = dup(STDIN_FILENO);
    assert(savedStdin != -1);
    assert(dup2(pipefd[0], STDIN_FILENO) != -1);
    close(pipefd[0]);

    int key = ReadKey();

    assert(dup2(savedStdin, STDIN_FILENO) != -1);
    close(savedStdin);
    return key;
}

void test_ReadKey_plain_char(void) {
    assert(ReadKeyFromBytes("a", 1) == 'a');
}

void test_ReadKey_arrows(void) {
    assert(ReadKeyFromBytes("\x1b[A", 3) == ARROW_UP);
    assert(ReadKeyFromBytes("\x1b[B", 3) == ARROW_DOWN);
    assert(ReadKeyFromBytes("\x1b[C", 3) == ARROW_RIGHT);
    assert(ReadKeyFromBytes("\x1b[D", 3) == ARROW_LEFT);
}

void test_ReadKey_home_end_variants(void) {
    assert(ReadKeyFromBytes("\x1b[H", 3) == HOME_KEY);
    assert(ReadKeyFromBytes("\x1b[F", 3) == END_KEY);
    assert(ReadKeyFromBytes("\x1bOH", 3) == HOME_KEY);
    assert(ReadKeyFromBytes("\x1bOF", 3) == END_KEY);
    assert(ReadKeyFromBytes("\x1b[1~", 4) == HOME_KEY);
    assert(ReadKeyFromBytes("\x1b[4~", 4) == END_KEY);
    assert(ReadKeyFromBytes("\x1b[7~", 4) == HOME_KEY);
    assert(ReadKeyFromBytes("\x1b[8~", 4) == END_KEY);
}

void test_ReadKey_delete_and_page_keys(void) {
    assert(ReadKeyFromBytes("\x1b[3~", 4) == DELETE_KEY);
    assert(ReadKeyFromBytes("\x1b[5~", 4) == PAGE_UP);
    assert(ReadKeyFromBytes("\x1b[6~", 4) == PAGE_DOWN);
}

void test_ReadKey_ctrl_and_shift_arrows(void) {
    assert(ReadKeyFromBytes("\x1b[1;5C", 6) == CTRL_ARROW_RIGHT);
    assert(ReadKeyFromBytes("\x1b[1;5D", 6) == CTRL_ARROW_LEFT);
    assert(ReadKeyFromBytes("\x1b[1;2A", 6) == SHIFT_ARROW_UP);
    assert(ReadKeyFromBytes("\x1b[1;2B", 6) == SHIFT_ARROW_DOWN);
    assert(ReadKeyFromBytes("\x1b[1;2C", 6) == SHIFT_ARROW_RIGHT);
    assert(ReadKeyFromBytes("\x1b[1;2D", 6) == SHIFT_ARROW_LEFT);
}

void test_ReadKey_alt_s_and_alt_f(void) {
    assert(ReadKeyFromBytes("\x1b" "s", 2) == ALT_S);
    assert(ReadKeyFromBytes("\x1b" "S", 2) == ALT_S);
    assert(ReadKeyFromBytes("\x1b" "f", 2) == ALT_F);
    assert(ReadKeyFromBytes("\x1b" "F", 2) == ALT_F);
}

void test_ReadKey_lone_escape_returns_escape(void) {
    // No bytes follow the escape (immediate EOF) -- must return '\x1b' rather
    // than blocking or crashing.
    assert(ReadKeyFromBytes("\x1b", 1) == '\x1b');
}

void test_Terminal_GetCursorPosition(void) {
    int pipefd[2];
    assert(pipe(pipefd) == 0);
    const char* response = "\x1b[24;80R";
    assert(write(pipefd[1], response, strlen(response)) == (ssize_t)strlen(response));
    close(pipefd[1]);

    int savedStdin = dup(STDIN_FILENO);
    assert(savedStdin != -1);
    assert(dup2(pipefd[0], STDIN_FILENO) != -1);
    close(pipefd[0]);

    size_t rows = 0, columns = 0;
    bool ok = Terminal_GetCursorPosition(&rows, &columns);

    assert(dup2(savedStdin, STDIN_FILENO) != -1);
    close(savedStdin);

    assert(ok == true);
    assert(rows == 24);
    assert(columns == 80);
}

void test_Terminal_GetWindowSize(void) {
    // If stdout is a real TTY, TIOCGWINSZ succeeds and the real window size is
    // used instead of falling back to the cursor-position query below, so only
    // assert the environment-independent invariant: success with sane values.
    int pipefd[2];
    assert(pipe(pipefd) == 0);
    const char* response = "\x1b[24;80R";
    assert(write(pipefd[1], response, strlen(response)) == (ssize_t)strlen(response));
    close(pipefd[1]);

    int savedStdin = dup(STDIN_FILENO);
    assert(savedStdin != -1);
    assert(dup2(pipefd[0], STDIN_FILENO) != -1);
    close(pipefd[0]);

    size_t rows = 0, columns = 0;
    bool ok = Terminal_GetWindowSize(&rows, &columns);

    assert(dup2(savedStdin, STDIN_FILENO) != -1);
    close(savedStdin);

    assert(ok == true);
    assert(rows > 0);
    assert(columns > 0);
}

void test_ReadKey_new_keys_and_modifiers(void) {
    // F-keys (F1 - F4: SS3 and CSI sequences)
    assert(ReadKeyFromBytes("\x1bOP", 3) == KEY_F1);
    assert(ReadKeyFromBytes("\x1bOQ", 3) == KEY_F2);
    assert(ReadKeyFromBytes("\x1bOR", 3) == KEY_F3);
    assert(ReadKeyFromBytes("\x1bOS", 3) == KEY_F4);

    assert(ReadKeyFromBytes("\x1b[1;5P", 6) == (KEY_F1 | KEY_MOD_CTRL));
    assert(ReadKeyFromBytes("\x1b[1;2Q", 6) == (KEY_F2 | KEY_MOD_SHIFT));
    assert(ReadKeyFromBytes("\x1b[1;3R", 6) == (KEY_F3 | KEY_MOD_ALT));
    assert(ReadKeyFromBytes("\x1b[1;4S", 6) == (KEY_F4 | KEY_MOD_SHIFT | KEY_MOD_ALT));

    // F-keys (F5 - F12)
    assert(ReadKeyFromBytes("\x1b[15~", 5) == KEY_F5);
    assert(ReadKeyFromBytes("\x1b[17~", 5) == KEY_F6);
    assert(ReadKeyFromBytes("\x1b[18~", 5) == KEY_F7);
    assert(ReadKeyFromBytes("\x1b[19~", 5) == KEY_F8);
    assert(ReadKeyFromBytes("\x1b[20~", 5) == KEY_F9);
    assert(ReadKeyFromBytes("\x1b[21~", 5) == KEY_F10);
    assert(ReadKeyFromBytes("\x1b[23~", 5) == KEY_F11);
    assert(ReadKeyFromBytes("\x1b[24~", 5) == KEY_F12);

    assert(ReadKeyFromBytes("\x1b[15;5~", 7) == (KEY_F5 | KEY_MOD_CTRL));
    assert(ReadKeyFromBytes("\x1b[24;2~", 7) == (KEY_F12 | KEY_MOD_SHIFT));

    // Insert key
    assert(ReadKeyFromBytes("\x1b[2~", 4) == INSERT_KEY);
    assert(ReadKeyFromBytes("\x1b[2;5~", 6) == (INSERT_KEY | KEY_MOD_CTRL));

    // Alt-keys general translation
    assert(ReadKeyFromBytes("\x1b" "a", 2) == ('a' | KEY_MOD_ALT));
    assert(ReadKeyFromBytes("\x1b" "A", 2) == ('a' | KEY_MOD_ALT));
    assert(ReadKeyFromBytes("\x1b" "z", 2) == ('z' | KEY_MOD_ALT));

    // modifyOtherKeys format (CSI 27 ; <mod> ; <char> ~)
    // Ctrl+Tab
    assert(ReadKeyFromBytes("\x1b[27;5;9~", 9) == ('\t' | KEY_MOD_CTRL));
    // Ctrl+Shift+a -> CTRL_KEY('a') | KEY_MOD_SHIFT
    assert(ReadKeyFromBytes("\x1b[27;6;97~", 10) == (CTRL_KEY('a') | KEY_MOD_SHIFT));

    // CSI u format uses Kitty's 1+bitmask modifier encoding:
    // 2=Shift, 3=Alt, 5=Ctrl, 8=Ctrl+Alt+Shift.
    // Ctrl+Tab (mod=5)
    assert(ReadKeyFromBytes("\x1b[9;5u", 6) == ('\t' | KEY_MOD_CTRL));
    // Ctrl+Alt+Shift+f (mod=8)
    assert(ReadKeyFromBytes("\x1b[102;8u", 8) == (CTRL_KEY('f') | KEY_MOD_ALT | KEY_MOD_SHIFT));

    // Ghostty's CSI-u encoding for editor control bindings.
    assert(ReadKeyFromBytes("\x1b[99;5u", 7) == CTRL_KEY('c'));
    assert(ReadKeyFromBytes("\x1b[113;5u", 8) == CTRL_KEY('q'));
    assert(ReadKeyFromBytes("\x1b[118;5u", 8) == CTRL_KEY('v'));
    assert(ReadKeyFromBytes("\x1b[120;5u", 8) == CTRL_KEY('x'));
    assert(ReadKeyFromBytes("\x1b[121;5u", 8) == CTRL_KEY('y'));
    assert(ReadKeyFromBytes("\x1b[122;5u", 8) == CTRL_KEY('z'));

    // Shift+Home/End use CSI 1 ; modifier H/F.
    assert(ReadKeyFromBytes("\x1b[1;2H", 6) == SHIFT_HOME_KEY);
    assert(ReadKeyFromBytes("\x1b[1;2F", 6) == SHIFT_END_KEY);

    // Shift+Tab (Z)
    assert(ReadKeyFromBytes("\x1b[Z", 3) == ('\t' | KEY_MOD_SHIFT));
}
