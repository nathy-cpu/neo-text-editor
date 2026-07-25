// Must come before any system header (even <stdio.h> below): glibc's
// <features.h> computes its internal __USE_* visibility macros the first time
// it's pulled in and then guards against reprocessing, so defining these
// afterwards -- e.g. inside subsystem headers -- would be too late to unlock
// POSIX/GNU declarations (strdup, setenv, unsetenv, ...) for the rest of this
// translation unit.
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "harness.c"

// Test order follows include order (constructors run in declaration order
// within this single translation unit).
#include "test_file_io.c"
#include "test_terminal.c"
#include "test_gap_buffer.c"
#include "test_array.c"
#include "test_slice.c"
#include "test_line.c"
#include "test_buffer.c"
#include "test_config.c"
#include "test_args.c"
#include "test_log.c"
#include "test_history.c"
#include "test_syntax.c"
#include "test_input.c"
#include "test_utf8.c"

// Regression suites
#include "test_regression_terminal.c"
#include "test_signals.c"
#include "test_regression_render.c"
#include "test_regression_input.c"
#include "test_regression_buffer.c"
#include "test_regression_file_io.c"
#include "test_regression_syntax.c"
#include "test_regression_config.c"

int main(int argc, char** argv)
{
    // No Logger_Init here: the logger tests manage the global logger's
    // lifecycle themselves and several assert on the uninitialized state,
    // which each fork()ed child inherits from this process.
    return HarnessMain(argc, argv);
}
