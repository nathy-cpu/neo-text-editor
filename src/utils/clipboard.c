#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "clipboard.h"
#include "array.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Array internalClipboard;
static Array tempClipboardBuffer;
static bool clipboardInitialized = false;

static void EnsureClipboardInitialized(void)
{
    if (!clipboardInitialized) {
        Array_InitChar(&internalClipboard, 1024);
        Array_InitChar(&tempClipboardBuffer, 1024);
        clipboardInitialized = true;
    }
}

void Clipboard_BeginWrite(void)
{
    EnsureClipboardInitialized();
    Array_Clear(&internalClipboard);
}

void Clipboard_Append(const char* data, size_t size)
{
    if (!data || size == 0)
        return;
    EnsureClipboardInitialized();
    Array_Append(&internalClipboard, data, size);
}

void Clipboard_EndWrite(void)
{
    EnsureClipboardInitialized();

    // Add hidden null terminator
    char nullChar = '\0';
    Array_Append(&internalClipboard, &nullChar, 1);
    internalClipboard.size--;

    const char* text = (const char*)internalClipboard.data;
    size_t len = internalClipboard.size;

    // Try wl-copy (Wayland)
    FILE* pipe = popen("wl-copy 2>/dev/null", "w");
    if (pipe) {
        fwrite(text, 1, len, pipe);
        if (pclose(pipe) == 0) {
            return;
        }
    }

    // Try xclip (X11)
    pipe = popen("xclip -selection clipboard 2>/dev/null", "w");
    if (pipe) {
        fwrite(text, 1, len, pipe);
        if (pclose(pipe) == 0) {
            return;
        }
    }

    // Try xsel (X11 fallback)
    pipe = popen("xsel --clipboard --input 2>/dev/null", "w");
    if (pipe) {
        fwrite(text, 1, len, pipe);
        pclose(pipe);
    }
}

void Clipboard_Write(const char* text)
{
    if (!text)
        return;
    Clipboard_BeginWrite();
    Clipboard_Append(text, strlen(text));
    Clipboard_EndWrite();
}

Slice Clipboard_Read(void)
{
    EnsureClipboardInitialized();

    bool systemReadSuccess = false;
    Array_Clear(&tempClipboardBuffer);

    // Try wl-paste (Wayland)
    FILE* pipe = popen("wl-paste -n 2>/dev/null", "r");
    if (pipe) {
        char chunk[512];
        size_t bytesRead;
        while ((bytesRead = fread(chunk, 1, sizeof(chunk), pipe)) > 0) {
            Array_Append(&tempClipboardBuffer, chunk, bytesRead);
        }
        if (pclose(pipe) == 0 && Array_Size(&tempClipboardBuffer) > 0) {
            systemReadSuccess = true;
        } else {
            Array_Clear(&tempClipboardBuffer);
        }
    }

    // Try xclip (X11)
    if (!systemReadSuccess) {
        pipe = popen("xclip -selection clipboard -o 2>/dev/null", "r");
        if (pipe) {
            char chunk[512];
            size_t bytesRead;
            while ((bytesRead = fread(chunk, 1, sizeof(chunk), pipe)) > 0) {
                Array_Append(&tempClipboardBuffer, chunk, bytesRead);
            }
            if (pclose(pipe) == 0 && Array_Size(&tempClipboardBuffer) > 0) {
                systemReadSuccess = true;
            } else {
                Array_Clear(&tempClipboardBuffer);
            }
        }
    }

    // Try xsel (X11 fallback)
    if (!systemReadSuccess) {
        pipe = popen("xsel --clipboard --output 2>/dev/null", "r");
        if (pipe) {
            char chunk[512];
            size_t bytesRead;
            while ((bytesRead = fread(chunk, 1, sizeof(chunk), pipe)) > 0) {
                Array_Append(&tempClipboardBuffer, chunk, bytesRead);
            }
            if (pclose(pipe) == 0 && Array_Size(&tempClipboardBuffer) > 0) {
                systemReadSuccess = true;
            } else {
                Array_Clear(&tempClipboardBuffer);
            }
        }
    }

    if (systemReadSuccess) {
        // Overwrite internalClipboard with tempClipboardBuffer contents
        Array_Clear(&internalClipboard);
        Array_Append(&internalClipboard, tempClipboardBuffer.data, tempClipboardBuffer.size);

        // Add a hidden null terminator
        char nullChar = '\0';
        Array_Append(&internalClipboard, &nullChar, 1);
        internalClipboard.size--;
    }

    return Array_ToSlice(&internalClipboard);
}

void Clipboard_Free(void)
{
    if (clipboardInitialized) {
        Array_Free(&internalClipboard);
        Array_Free(&tempClipboardBuffer);
        clipboardInitialized = false;
    }
}
