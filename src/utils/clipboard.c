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
#include "../platform/platform.h"
#include "array.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Array internalClipboard;
static Array tempClipboardBuffer;
static bool clipboardInitialized = false;
static const Platform* clipboardPlatform = NULL;

void Clipboard_SetPlatform(const struct Platform* platform) { clipboardPlatform = platform; }

static const Platform* ClipboardPlatform(void)
{
    if (!clipboardPlatform)
        clipboardPlatform = Platform_Default();
    return clipboardPlatform;
}

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

    const Platform* platform = ClipboardPlatform();
    platform->clipboardCopy(platform->context, text, len);
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

    Array_Clear(&tempClipboardBuffer);
    const Platform* platform = ClipboardPlatform();
    bool systemReadSuccess = platform->clipboardPaste(platform->context, &tempClipboardBuffer);

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
