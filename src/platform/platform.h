#pragma once

#include "../utils/array.h"
#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

/**
 * Platform - the editor's only gateway to the outside world.
 *
 * Every external touchpoint (key input, screen output, window geometry,
 * system clipboard) goes through these function pointers so tests can swap
 * in fakes: scripted key streams, captured frames, an in-memory clipboard,
 * and pinned window dimensions.
 */
typedef struct Platform {
    void* context;

    /** Next decoded keypress (production: wraps ReadKey()). */
    int (*readKey)(void* context);

    /** Screen output (production: write() to STDOUT_FILENO). */
    ssize_t (*writeTerminal)(void* context, const void* buffer, size_t size);

    /** Window dimensions (production: Terminal_GetWindowSize). */
    bool (*getWindowSize)(void* context, size_t* rows, size_t* columns);

    /**
     * System clipboard. Return false when no system clipboard is available
     * or the operation failed; callers fall back to the internal clipboard.
     */
    bool (*clipboardCopy)(void* context, const char* data, size_t size);
    bool (*clipboardPaste)(void* context, Array* destination);
} Platform;

/** The production platform (real terminal, real clipboard tools). */
const Platform* Platform_Default(void);
