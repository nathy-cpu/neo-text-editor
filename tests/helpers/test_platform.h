// TestPlatform - Platform fake for driving the editor without a terminal.
//
// Usage:
//     TestPlatform testPlatform;
//     TestPlatform_Init(&testPlatform);
//     Editor editor;
//     Editor_Init(&editor);
//     TestPlatform_Attach(&testPlatform, &editor);
//     static const int keys[] = { 'a', 'b', '\r' };
//     TestPlatform_SetKeys(&testPlatform, keys, 3);
//     ... drive Editor_ProcessKeypress / Editor_RefreshScreen ...
//     TestPlatform_Free(&testPlatform);
//
// readKey aborts when the scripted key list is exhausted -- under the fork
// harness that is a clean per-test FAIL rather than a hang, so every scripted
// interaction (especially Editor_Prompt loops) MUST end in a key that
// terminates the flow ('\r', ESC, a quit key, ...).
#ifndef TEST_HELPERS_TEST_PLATFORM_H
#define TEST_HELPERS_TEST_PLATFORM_H

#include "../../src/features/editor.h"
#include "../../src/platform/platform.h"
#include "../../src/utils/array.h"
#include "../../src/utils/clipboard.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct TestPlatform {
    Platform platform; // platform.context points back at this struct
    const int* keys; // scripted decoded keypresses
    size_t keyCount;
    size_t keyCursor;
    Array screen; // every byte "written" to the terminal, in order
    Array clipboard; // fake system clipboard contents
    bool clipboardAvailable; // false => exercise the internal-clipboard fallback
    size_t rows, columns; // reported window size (default 24 x 80)
} TestPlatform;

static inline int TestPlatform_ReadKey(void* context)
{
    TestPlatform* testPlatform = (TestPlatform*)context;
    if (testPlatform->keyCursor >= testPlatform->keyCount) {
        fprintf(stderr, "TestPlatform: scripted key list exhausted after %zu keys\n",
            testPlatform->keyCount);
        abort();
    }
    return testPlatform->keys[testPlatform->keyCursor++];
}

static inline ssize_t TestPlatform_WriteTerminal(void* context, const void* buffer, size_t size)
{
    TestPlatform* testPlatform = (TestPlatform*)context;
    Array_Append(&testPlatform->screen, buffer, size);
    return (ssize_t)size;
}

static inline bool TestPlatform_GetWindowSize(void* context, size_t* rows, size_t* columns)
{
    TestPlatform* testPlatform = (TestPlatform*)context;
    *rows = testPlatform->rows;
    *columns = testPlatform->columns;
    return true;
}

static inline bool TestPlatform_ClipboardCopy(void* context, const char* data, size_t size)
{
    TestPlatform* testPlatform = (TestPlatform*)context;
    if (!testPlatform->clipboardAvailable)
        return false;
    Array_Clear(&testPlatform->clipboard);
    Array_Append(&testPlatform->clipboard, data, size);
    return true;
}

static inline bool TestPlatform_ClipboardPaste(void* context, Array* destination)
{
    TestPlatform* testPlatform = (TestPlatform*)context;
    if (!testPlatform->clipboardAvailable || Array_Size(&testPlatform->clipboard) == 0)
        return false;
    Array_Clear(destination);
    Array_Append(destination, testPlatform->clipboard.data, testPlatform->clipboard.size);
    return true;
}

static inline void TestPlatform_Init(TestPlatform* testPlatform)
{
    memset(testPlatform, 0, sizeof(*testPlatform));
    testPlatform->platform.context = testPlatform;
    testPlatform->platform.readKey = TestPlatform_ReadKey;
    testPlatform->platform.writeTerminal = TestPlatform_WriteTerminal;
    testPlatform->platform.getWindowSize = TestPlatform_GetWindowSize;
    testPlatform->platform.clipboardCopy = TestPlatform_ClipboardCopy;
    testPlatform->platform.clipboardPaste = TestPlatform_ClipboardPaste;
    Array_InitChar(&testPlatform->screen, 4096);
    Array_InitChar(&testPlatform->clipboard, 256);
    testPlatform->clipboardAvailable = true;
    testPlatform->rows = 24;
    testPlatform->columns = 80;
}

static inline void TestPlatform_SetKeys(TestPlatform* testPlatform, const int* keys, size_t keyCount)
{
    testPlatform->keys = keys;
    testPlatform->keyCount = keyCount;
    testPlatform->keyCursor = 0;
}

static inline void TestPlatform_Attach(TestPlatform* testPlatform, Editor* editor)
{
    editor->platform = &testPlatform->platform;
    Clipboard_SetPlatform(&testPlatform->platform);
}

static inline void TestPlatform_Free(TestPlatform* testPlatform)
{
    Array_Free(&testPlatform->screen);
    Array_Free(&testPlatform->clipboard);
    // Detach the clipboard singleton from this (about to be invalid) platform.
    Clipboard_SetPlatform(NULL);
}

// True when `needle` appears anywhere in the captured terminal output.
static inline bool TestScreen_Contains(const TestPlatform* testPlatform, const char* needle)
{
    size_t screenSize = Array_Size((Array*)&testPlatform->screen);
    size_t needleLength = strlen(needle);
    if (needleLength == 0 || screenSize < needleLength)
        return false;
    const char* screenData = (const char*)testPlatform->screen.data;
    for (size_t i = 0; i + needleLength <= screenSize; i++) {
        if (memcmp(screenData + i, needle, needleLength) == 0)
            return true;
    }
    return false;
}

#endif // TEST_HELPERS_TEST_PLATFORM_H
