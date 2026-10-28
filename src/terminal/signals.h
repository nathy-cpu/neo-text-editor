#pragma once

struct Editor;

/**
 * Installs the process signal handlers and remembers the editor that
 * SignalsCleanup acts on. SIGINT, SIGTERM and SIGWINCH only set flags (read
 * by ReadKey) and are installed without SA_RESTART so a blocked read() is
 * interrupted promptly. SIGSEGV, SIGBUS, SIGFPE and SIGABRT restore the
 * terminal and re-raise; SIGPIPE is ignored.
 */
void SignalsInstall(struct Editor* editor);

/** The signal handler body, exported so tests can invoke it directly. */
void SignalsHandleSignal(int signalNumber);

/**
 * Full shutdown cleanup (restore terminal, free editor/logger/clipboard).
 * Register with atexit() once raw mode is enabled.
 */
void SignalsCleanup(void);
