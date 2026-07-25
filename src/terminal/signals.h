#pragma once

struct Editor;

/**
 * Installs the process signal handlers (SIGINT, SIGTERM, SIGWINCH) and
 * remembers the editor they act on. SIGWINCH is installed without SA_RESTART
 * so a blocked read() is interrupted promptly.
 */
void SignalsInstall(struct Editor* editor);

/** The signal handler body, exported so tests can invoke it directly. */
void SignalsHandleSignal(int signalNumber);

/**
 * Full shutdown cleanup (restore terminal, free editor/logger/clipboard).
 * Register with atexit() once raw mode is enabled.
 */
void SignalsCleanup(void);
