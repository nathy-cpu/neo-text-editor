// Regression tests for escape-sequence parsing in the terminal layer (suite
// regression_terminal). Each test pins decoding behavior that has regressed before.
//
// Single-TU build: this file is #included into tests/main.c (no main() here).

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "../src/terminal/terminal.h"
#include "../src/terminal/terminal_internal.h"
#include "helpers/test_io.h"
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

// ---------------------------------------------------------------------------
// Shift-only printables decode to the plain shifted/uppercased character with
// NO shift flag. A value carrying `code | KEY_MOD_SHIFT` (>= 128) would be
// dropped by input.c's `!iscntrl && < 128` printable filter, making capitals
// untypeable on xterm/VTE (modifyOtherKeys) and kitty.
// ---------------------------------------------------------------------------

TEST(regression_terminal, shifted_printable_xterm_modify_other_keys) {
    // xterm modifyOtherKeys: CSI 27 ; 2 ; 72 ~ -- already-shifted keysym 'H'
    // with the shift modifier. This decodes to plain 'H'.
    assert(ReadKeyFromBytes("\x1b[27;2;72~", 10) == 'H');
}

TEST(regression_terminal, shifted_printable_kitty_csi_u_uppercases) {
    // kitty CSI-u: base key 'h' (104) + shift modifier -> uppercased 'H'.
    assert(ReadKeyFromBytes("\x1b[104;2u", 8) == 'H');
}

TEST(regression_terminal, shifted_punctuation_kitty_csi_u_plain) {
    // Already-shifted punctuation keysym '!' (33) with shift modifier must
    // come back as plain '!'.
    assert(ReadKeyFromBytes("\x1b[33;2u", 7) == '!');
}

TEST(regression_terminal, map_modified_key_code_shift_only_folds) {
    // Direct unit seam: shift folds into the character instead of surviving
    // as a flag that pushes the value past input.c's < 128 filter.
    assert(TerminalMapModifiedKeyCode('h', KEY_MOD_SHIFT) == 'H');
    assert(TerminalMapModifiedKeyCode('A', KEY_MOD_SHIFT) == 'A');
}

// ---------------------------------------------------------------------------
// TerminalMapCharCodeToKey must not hijack the printable letters 'H' (72) and
// 'F' (70) as HOME_KEY/END_KEY -- that would turn Shift+H, Alt+F, etc. into
// cursor motion.
// ---------------------------------------------------------------------------

TEST(regression_terminal, map_char_code_letter_h_not_home) {
    assert(TerminalMapCharCodeToKey('H') == 'H');
}

TEST(regression_terminal, map_char_code_letter_f_not_end) {
    assert(TerminalMapCharCodeToKey('F') == 'F');
}

TEST(regression_terminal, shift_h_keysym_not_hijacked_as_home) {
    // End-to-end: Shift+H via modifyOtherKeys must not decode to HOME_KEY.
    // The comparison masks off modifier flags so a flagged variant such as
    // HOME_KEY | KEY_MOD_SHIFT cannot slip past the assert either.
    int key = ReadKeyFromBytes("\x1b[27;2;72~", 10);
    int modifierMask = KEY_MOD_SHIFT | KEY_MOD_ALT | KEY_MOD_CTRL;
    assert((key & ~modifierMask) != HOME_KEY);
}

// ---------------------------------------------------------------------------
// The functional-keycode table must match the kitty keyboard protocol.
// Real kitty functional codes: 57423=Home, 57424=End, 57417=Left,
// 57421=PageUp, ... while 57360 (0xE010) is NumLock and 57376 (0xE020) is
// F13 -- neither may map to an editor key.
// ---------------------------------------------------------------------------

TEST(regression_terminal, kitty_functional_home_and_end) {
    assert(ReadKeyFromBytes("\x1b[57423u", 8) == HOME_KEY);
    assert(ReadKeyFromBytes("\x1b[57424u", 8) == END_KEY);
}

TEST(regression_terminal, kitty_functional_arrow_and_page) {
    assert(ReadKeyFromBytes("\x1b[57417u", 8) == ARROW_LEFT);
    assert(ReadKeyFromBytes("\x1b[57421u", 8) == PAGE_UP);
}

TEST(regression_terminal, kitty_num_lock_not_mapped_to_home) {
    // 57360 is NumLock in the kitty protocol; a table keyed from 0xE010
    // upward would misread it as HOME_KEY.
    assert(ReadKeyFromBytes("\x1b[57360u", 8) != HOME_KEY);
}

TEST(regression_terminal, kitty_f13_not_mapped_to_f1) {
    // 57376 is F13 in the kitty protocol; a table keyed from 0xE020 upward
    // would misread it as KEY_F1.
    assert(ReadKeyFromBytes("\x1b[57376u", 8) != KEY_F1);
}

// ---------------------------------------------------------------------------
// TerminalGetModifierFlags: any modifier parameter <= 1 yields 0. Computing
// `parameter - 1` for parameter 0 would give -1, whose low bits set ALL
// modifier flags.
// ---------------------------------------------------------------------------

TEST(regression_terminal, modifier_parameter_zero_means_no_modifiers) {
    assert(TerminalGetModifierFlags(0) == 0);
}

TEST(regression_terminal, modifier_parameter_sanity) {
    // Pins the standard encoding: parameter 1 means no modifiers, 2 is Shift.
    assert(TerminalGetModifierFlags(1) == 0);
    assert(TerminalGetModifierFlags(2) == KEY_MOD_SHIFT);
}

TEST(regression_terminal, csi_u_zero_modifier_parameter_plain_key) {
    // CSI 97 ; 0 u -- an explicit zero modifier parameter must decode to a
    // plain 'a', not Ctrl+Alt+Shift+A.
    assert(ReadKeyFromBytes("\x1b[97;0u", 7) == 'a');
}

// ---------------------------------------------------------------------------
// CSI parameter accumulation (`params[n] * 10 + digit`) is clamped at 65535,
// so an absurdly long numeric parameter such as "99999999999999999999" cannot
// overflow int (signed-overflow UB). Clamped input decodes deterministically:
// a huge modifier parameter becomes some combination of modifier flags on the
// correct base key, so the assert masks the flags off and checks only the key.
// ---------------------------------------------------------------------------

TEST(regression_terminal, csi_parameter_overflow_is_clamped) {
    int key = ReadKeyFromBytes("\x1b[1;99999999999999999999A", 25);
    assert((key & ~(KEY_MOD_SHIFT | KEY_MOD_ALT | KEY_MOD_CTRL)) == ARROW_UP);
}

// ---------------------------------------------------------------------------
// The `windowResized` flag is checked at the top of ReadKey's wait loop, not
// only on the EINTR path, so a resize that lands while ReadKey is NOT blocked
// in read() is still seen: the first ReadKey after a resize returns
// RESIZE_EVENT even when input bytes are already waiting.
// ---------------------------------------------------------------------------

TEST(regression_terminal, resize_flag_seen_without_eintr) {
    windowResized = 1;
    int firstKey = ReadKeyFromBytes("a", 1); // pending resize wins over 'a'
    assert(firstKey == RESIZE_EVENT);
    assert(windowResized == 0);
    int secondKey = ReadKeyFromBytes("a", 1);
    assert(secondKey == 'a');
    windowResized = 0;
}

// ---------------------------------------------------------------------------
// An EINTR during the continuation reads of an escape sequence must not abort
// the sequence -- ReadKey would return a bare ESC and the tail bytes would be
// typed into the buffer as text. Continuation reads retry on EINTR so the
// full sequence is decoded.
// ---------------------------------------------------------------------------

static void RegressionTerminalNoOpSignalHandler(int signalNumber)
{
    (void)signalNumber;
}

static void RegressionTerminalSleepMilliseconds(long milliseconds)
{
    struct timespec duration = { milliseconds / 1000, (milliseconds % 1000) * 1000000L };
    nanosleep(&duration, NULL);
}

TEST(regression_terminal, eintr_mid_escape_completes_sequence) {
    // A no-op handler installed WITHOUT SA_RESTART, so a blocked read() is
    // interrupted with EINTR (mirrors how SIGWINCH is installed in signals.c).
    struct sigaction interruptAction;
    interruptAction.sa_handler = RegressionTerminalNoOpSignalHandler;
    sigemptyset(&interruptAction.sa_mask);
    interruptAction.sa_flags = 0;
    assert(sigaction(SIGUSR1, &interruptAction, NULL) == 0);

    windowResized = 0; // the EINTR must not be mistaken for a resize

    int pipeFds[2];
    assert(pipe(pipeFds) == 0);

    pid_t writerPid = fork();
    assert(writerPid >= 0);
    if (writerPid == 0) {
        // Writer child: start an Up-arrow sequence, park the reader blocked
        // mid-sequence, interrupt it, then deliver the final byte.
        close(pipeFds[0]);
        assert(write(pipeFds[1], "\x1b[", 2) == 2);
        RegressionTerminalSleepMilliseconds(150); // reader is now blocked mid-sequence
        kill(getppid(), SIGUSR1);
        RegressionTerminalSleepMilliseconds(50);
        kill(getppid(), SIGUSR1); // second shot in case the first landed early
        RegressionTerminalSleepMilliseconds(50);
        assert(write(pipeFds[1], "A", 1) == 1);
        close(pipeFds[1]);
        _exit(0);
    }

    close(pipeFds[1]);
    int savedStdin = dup(STDIN_FILENO);
    assert(savedStdin != -1);
    assert(dup2(pipeFds[0], STDIN_FILENO) != -1);
    close(pipeFds[0]);

    int key = ReadKey(); // must survive the mid-sequence EINTR

    assert(dup2(savedStdin, STDIN_FILENO) != -1);
    close(savedStdin);

    int writerStatus = 0;
    pid_t waitedPid;
    do {
        waitedPid = waitpid(writerPid, &writerStatus, 0);
    } while (waitedPid == -1 && errno == EINTR);
    assert(waitedPid == writerPid);

    signal(SIGUSR1, SIG_DFL);

    assert(key == ARROW_UP);
}

// ---------------------------------------------------------------------------
// ReadKey returns plain bytes as unsigned values (0xC3 == 195). Returning
// them through a signed char would make byte 0xC3 (first byte of a UTF-8
// sequence) come back negative.
// ---------------------------------------------------------------------------

TEST(regression_terminal, high_byte_returned_unsigned) {
    assert(ReadKeyFromBytes("\xC3", 1) == 0xC3);
}
