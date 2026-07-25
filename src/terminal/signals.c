#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "signals.h"
#include "../features/editor.h"
#include "../utils/clipboard.h"
#include "../utils/logger.h"
#include "terminal.h"

#include <signal.h>
#include <stddef.h>
#include <stdlib.h>

static struct Editor* signalsEditor = NULL;

void SignalsCleanup(void)
{
    if (!signalsEditor)
        return;
    Editor_RestoreTerminal(signalsEditor);
    Editor_Free(signalsEditor);
    Logger_Free();
    Clipboard_Free();
    signalsEditor = NULL;
}

void SignalsHandleSignal(int signalNumber)
{
    // Async-signal-safe only: set flags, nothing else. The main loop notices
    // them via ReadKey (TERMINATE_EVENT / RESIZE_EVENT) and exits or redraws
    // from normal context, where free()/fclose()/fflush() are legal.
    if (signalNumber == SIGWINCH) {
        windowResized = 1;
    } else {
        terminationRequested = 1;
    }
}

// Fatal signals: restore the terminal with async-safe calls only, then
// re-raise with the default disposition so the crash (and core dump) still
// happens. Never exit() here -- atexit cleanup must not run in signal context.
static void FatalSignalHandler(int signalNumber)
{
    Terminal_AsyncRestore();
    signal(signalNumber, SIG_DFL);
    raise(signalNumber);
}

void SignalsInstall(struct Editor* editor)
{
    signalsEditor = editor;

    struct sigaction flagAction;
    flagAction.sa_handler = SignalsHandleSignal;
    sigemptyset(&flagAction.sa_mask);
    flagAction.sa_flags = 0; // NO SA_RESTART: read() must return EINTR promptly
    sigaction(SIGINT, &flagAction, NULL);
    sigaction(SIGTERM, &flagAction, NULL);
    sigaction(SIGWINCH, &flagAction, NULL);

    struct sigaction fatalAction;
    fatalAction.sa_handler = FatalSignalHandler;
    sigemptyset(&fatalAction.sa_mask);
    fatalAction.sa_flags = 0;
    sigaction(SIGSEGV, &fatalAction, NULL);
    sigaction(SIGBUS, &fatalAction, NULL);
    sigaction(SIGFPE, &fatalAction, NULL);
    sigaction(SIGABRT, &fatalAction, NULL);

    // A dead clipboard-tool pipe (wl-copy/xclip exiting early) must surface
    // as EPIPE on write, not kill the editor with unsaved work.
    signal(SIGPIPE, SIG_IGN);
}
