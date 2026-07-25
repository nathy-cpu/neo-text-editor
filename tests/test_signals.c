// Regression tests for signal handling (suite regression_signals): termination
// signals, SIGWINCH, and the process-wide SIGPIPE disposition.
//
// Single-TU build: this file is #included into tests/main.c (no main() here).

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "../src/features/editor.h"
#include "../src/terminal/signals.h"
#include "../src/terminal/terminal.h"
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

// ---------------------------------------------------------------------------
// The SIGINT/SIGTERM handler must only set a termination flag and return --
// never call exit(), which would run async-signal-UNSAFE cleanup
// (free/fclose/fflush via atexit) from signal context. The process survives
// the signal and the main loop exits cleanly later.
//
// Shape: TEST_EXITS(..., 7) rather than a plain assertion, because the
// failure mode to guard against is itself a clean exit(0) inside the
// handler -- indistinguishable from success to an in-process assert. Only
// the distinctive child exit code proves control returned from raise() and
// reached the explicit exit(7).
// ---------------------------------------------------------------------------

TEST_EXITS(regression_signals, sigint_survives_until_explicit_exit, 7) {
    Editor editor;
    Editor_Init(&editor);
    SignalsInstall(&editor);

    raise(SIGINT); // the handler must set a flag and return, not exit()

    // Reached because the handler returned control after setting its flag.
    Editor_Free(&editor); // keep LeakSanitizer quiet on the clean path
    exit(7);
}

// ---------------------------------------------------------------------------
// The SIGWINCH handler only sets the volatile sig_atomic_t flag; pinned
// here so a signal-handling rework cannot regress it.
// ---------------------------------------------------------------------------

TEST(regression_signals, sigwinch_sets_resize_flag) {
    windowResized = 0;
    SignalsHandleSignal(SIGWINCH);
    assert(windowResized == 1);
    windowResized = 0;
}

// ---------------------------------------------------------------------------
// SIGPIPE must be ignored process-wide after SignalsInstall: without that
// disposition, a clipboard popen() whose reader is gone would kill the
// whole editor via the default SIGPIPE action. Writes to a broken pipe
// must fail with EPIPE instead.
// ---------------------------------------------------------------------------

TEST(regression_signals, sigpipe_ignored_after_install) {
    Editor editor;
    Editor_Init(&editor);
    SignalsInstall(&editor);

    int pipeFds[2];
    assert(pipe(pipeFds) == 0);
    close(pipeFds[0]); // no reader: the next write hits a broken pipe

    errno = 0;
    ssize_t written = write(pipeFds[1], "x", 1); // must fail with EPIPE, not raise SIGPIPE
    assert(written == -1);
    assert(errno == EPIPE);

    close(pipeFds[1]);
    Editor_Free(&editor);
}
