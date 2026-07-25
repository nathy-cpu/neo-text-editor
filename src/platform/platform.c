#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "platform.h"
#include "../terminal/terminal.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>

static int PlatformReadKey(void* context)
{
    (void)context;
    return ReadKey();
}

static ssize_t PlatformWriteTerminal(void* context, const void* buffer, size_t size)
{
    (void)context;
    return write(STDOUT_FILENO, buffer, size);
}

static bool PlatformGetWindowSize(void* context, size_t* rows, size_t* columns)
{
    (void)context;
    return Terminal_GetWindowSize(rows, columns);
}

// System clipboard tool chains (moved verbatim from clipboard.c). Each tool is
// tried in order; a tool "succeeds" when the pipe opens and pclose reports
// exit status 0 (and, for paste, produced at least one byte).

// Writes the full payload, retrying on EINTR (SIGWINCH is installed without
// SA_RESTART, so a resize mid-transfer interrupts stdio calls). With SIGPIPE
// ignored process-wide, a dead tool surfaces as a short write + EPIPE here
// instead of killing the editor.
static bool WriteAllToPipe(FILE* pipe, const char* data, size_t size)
{
    size_t written = 0;
    while (written < size) {
        size_t chunk = fwrite(data + written, 1, size - written, pipe);
        written += chunk;
        if (chunk == 0) {
            if (errno == EINTR) {
                clearerr(pipe);
                continue;
            }
            return false;
        }
    }
    return true;
}

static void ReadAllFromPipe(FILE* pipe, Array* destination)
{
    char chunk[512];
    for (;;) {
        size_t bytesRead = fread(chunk, 1, sizeof(chunk), pipe);
        if (bytesRead > 0)
            Array_Append(destination, chunk, bytesRead);
        if (bytesRead < sizeof(chunk)) {
            if (ferror(pipe) && errno == EINTR) {
                clearerr(pipe);
                continue;
            }
            return; // EOF or hard error
        }
    }
}

// pclose() == -1 with EINTR means the status was lost, not that the tool
// failed; treat it as success so we don't cascade into the next tool and
// clobber the clipboard a second time.
static bool PcloseSucceeded(int pcloseResult)
{
    if (pcloseResult == 0)
        return true;
    return pcloseResult == -1 && errno == EINTR;
}

static bool WriteToClipboardTool(const char* command, const char* data, size_t size)
{
    FILE* pipe = popen(command, "w");
    if (!pipe)
        return false;
    // Self-contained SIGPIPE protection: a clipboard tool that exits early
    // (missing wl-copy on X11, crashed xclip) must surface as EPIPE, not kill
    // the process with unsaved work. SignalsInstall also ignores SIGPIPE
    // globally, but this module must not depend on that having run.
    void (*previousPipeHandler)(int) = signal(SIGPIPE, SIG_IGN);
    bool wroteAll = WriteAllToPipe(pipe, data, size);
    int status = pclose(pipe);
    signal(SIGPIPE, previousPipeHandler);
    return wroteAll && PcloseSucceeded(status);
}

static bool PlatformClipboardCopy(void* context, const char* data, size_t size)
{
    (void)context;
    if (WriteToClipboardTool("wl-copy 2>/dev/null", data, size))
        return true;
    if (WriteToClipboardTool("xclip -selection clipboard 2>/dev/null", data, size))
        return true;
    return WriteToClipboardTool("xsel --clipboard --input 2>/dev/null", data, size);
}

static bool ReadFromClipboardTool(const char* command, Array* destination)
{
    FILE* pipe = popen(command, "r");
    if (!pipe)
        return false;
    ReadAllFromPipe(pipe, destination);
    if (PcloseSucceeded(pclose(pipe)) && Array_Size(destination) > 0)
        return true;
    Array_Clear(destination);
    return false;
}

static bool PlatformClipboardPaste(void* context, Array* destination)
{
    (void)context;
    Array_Clear(destination);
    if (ReadFromClipboardTool("wl-paste -n 2>/dev/null", destination))
        return true;
    if (ReadFromClipboardTool("xclip -selection clipboard -o 2>/dev/null", destination))
        return true;
    return ReadFromClipboardTool("xsel --clipboard --output 2>/dev/null", destination);
}

const Platform* Platform_Default(void)
{
    static const Platform defaultPlatform = {
        .context = NULL,
        .readKey = PlatformReadKey,
        .writeTerminal = PlatformWriteTerminal,
        .getWindowSize = PlatformGetWindowSize,
        .clipboardCopy = PlatformClipboardCopy,
        .clipboardPaste = PlatformClipboardPaste,
    };
    return &defaultPlatform;
}
