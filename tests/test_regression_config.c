// Regression tests for config-loading, clipboard, and CLI-args edge cases:
// unvalidated config values, Lua stack-lifetime hazards, dead clipboard
// consumers, and over-long jump arguments.

#include "../src/features/editor.h"
#include "../src/features/config.h"
#include "../src/features/syntax.h"
#include "../src/utils/array.h"
#include "../src/utils/clipboard.h"
#include "../src/utils/args.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void RegressionConfigWriteFile(const char* path, const char* contents)
{
    FILE* file = fopen(path, "w");
    assert(file != NULL);
    fputs(contents, file);
    fclose(file);
}

// Writes a temp config, loads it, and removes the file BEFORE any assertion
// can abort the run, so failures don't litter the repo directory.
// Returns Editor_LoadConfig's result; callers assert on it only where the
// expected return value is pinned down.
static bool RegressionConfigLoad(Editor* editor, const char* path, const char* contents)
{
    RegressionConfigWriteFile(path, contents);
    Editor_Init(editor);
    bool loaded = Editor_LoadConfig(editor, path);
    remove(path);
    return loaded;
}

// ---------------------------------------------------------------------------
// tab_size must be validated: applied verbatim, tab_size = 0 SIGFPEs on the
// first tab render (modulo by tabSize) and negative values wrap huge in
// size_t contexts. Policy: an out-of-range value (valid range 1..16) is
// REJECTED -- the config keeps the default (4) and an error is surfaced.
// ---------------------------------------------------------------------------

TEST(regression_config, tab_size_zero_rejected)
{
    Editor editor;
    // Return value deliberately not asserted: the load may either stay
    // "successful" with the bad key rejected, or fail as a whole.
    RegressionConfigLoad(&editor, "regression_config_tab_size_zero.lua", "tab_size = 0\n");
    assert(editor.config.tabSize == 4); // out of range: default kept
    Editor_Free(&editor);
}

TEST(regression_config, tab_size_negative_rejected)
{
    Editor editor;
    RegressionConfigLoad(&editor, "regression_config_tab_size_negative.lua", "tab_size = -3\n");
    assert(editor.config.tabSize == 4); // out of range: default kept
    Editor_Free(&editor);
}

TEST(regression_config, tab_size_seventeen_rejected)
{
    Editor editor;
    RegressionConfigLoad(&editor, "regression_config_tab_size_seventeen.lua", "tab_size = 17\n");
    assert(editor.config.tabSize == 4); // out of range: default kept
    Editor_Free(&editor);
}

// Upper edge of the valid range (1..16) must be accepted: guards the
// validation against over-rejection.
TEST(regression_config, tab_size_sixteen_accepted)
{
    Editor editor;
    assert(RegressionConfigLoad(&editor, "regression_config_tab_size_sixteen.lua", "tab_size = 16\n") == true);
    assert(editor.config.tabSize == 16);
    Editor_Free(&editor);
}

// ---------------------------------------------------------------------------
// GetLuaTableColor must strdup the lua_tostring result while its stack slot
// is still live. For a NUMERIC color value the coerced string is anchored
// only by that slot, so copying after the pop reads through a dangling
// pointer (masked in practice only because no Lua GC runs between the pop
// and the copy). The assertion pins that a numeric color value survives.
// ---------------------------------------------------------------------------

TEST(regression_config, numeric_color_value_survives)
{
    Editor editor;
    assert(RegressionConfigLoad(&editor, "regression_config_numeric_color.lua", "colors = { keyword = 35 }\n") == true);
    // Lua 5.4 coerces the integer 35 to the string "35" (no ".0" suffix).
    assert(editor.config.syntaxColors[HIGHLIGHT_KEYWORD] != NULL);
    assert(strcmp(editor.config.syntaxColors[HIGHLIGHT_KEYWORD], "35") == 0);
    Editor_Free(&editor);
}

// ---------------------------------------------------------------------------
// GetLuaStringArray must skip/compact non-string elements: writing NULL into
// the MIDDLE of the array makes NULL-terminated consumers (highlighting,
// Syntax_Free) stop early, so later entries are invisible AND leaked. A
// boolean is used as the hole -- a number would not do, since lua_isstring()
// accepts numbers and coerces them. With the hole compacted, "if" and "else"
// are adjacent.
// ---------------------------------------------------------------------------

TEST(regression_config, string_array_hole_compacted)
{
    Editor editor;
    bool loaded = RegressionConfigLoad(&editor, "regression_config_keyword_hole.lua",
        "languages = {\n"
        "  { name = \"Zed\", extensions = { \".zed\" }, keywords = { \"if\", true, \"else\" } },\n"
        "}\n");
    assert(loaded == true);

    Syntax* zedSyntax = NULL;
    for (size_t index = 0; index < Array_Size(&editor.config.syntaxDatabase); index++) {
        Syntax* syntax = (Syntax*)Array_At(&editor.config.syntaxDatabase, index);
        if (syntax->fileType && strcmp(syntax->fileType, "Zed") == 0) {
            zedSyntax = syntax;
            break;
        }
    }
    assert(zedSyntax != NULL);
    assert(zedSyntax->keywords != NULL);
    assert(zedSyntax->keywords[0] != NULL && strcmp(zedSyntax->keywords[0], "if") == 0);
    assert(zedSyntax->keywords[1] != NULL); // a NULL hole here would hide (and leak) "else"
    assert(strcmp(zedSyntax->keywords[1], "else") == 0);

    Editor_Free(&editor);
}

// ---------------------------------------------------------------------------
// When the config script raises a NON-STRING error object (error({}) makes
// lua_tostring return NULL), the error path must not feed NULL to %s /
// %.100s formatting -- that only happens to survive on glibc, which prints
// "(null)". This pins the required behavior: the load returns false and
// nothing crashes.
// ---------------------------------------------------------------------------

TEST(regression_config, nonstring_error_object_no_crash)
{
    Editor editor;
    assert(RegressionConfigLoad(&editor, "regression_config_nonstring_error.lua", "error({})\n") == false);
    Editor_Free(&editor);
}

// ---------------------------------------------------------------------------
// Suite regression_clipboard: the clipboard write path must ignore SIGPIPE and
// check fwrite. Every system tool is shimmed to a script that exits
// immediately, so the popen'd consumer is dead while the editor is still
// fwrite-ing a payload larger than the kernel pipe buffer: an unguarded
// blocked write takes SIGPIPE and kills the whole process. Clipboard_Write
// must return normally, every tool "fails", and the internal clipboard must
// still hold the data.
// ---------------------------------------------------------------------------

static void RegressionClipboardWriteShim(const char* directory, const char* toolName)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", directory, toolName);
    FILE* file = fopen(path, "w");
    assert(file != NULL);
    fputs("#!/bin/sh\nexit 1\n", file);
    fclose(file);
    assert(chmod(path, 0755) == 0);
}

static void RegressionClipboardRemoveShim(const char* directory, const char* toolName)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", directory, toolName);
    remove(path);
}

TEST(regression_clipboard, write_survives_dead_consumer)
{
    // Fake wl-copy/xclip/xsel (and wl-paste, so the Clipboard_Read below
    // cannot be satisfied by a real system clipboard) that exit at once.
    char shimDirTemplate[] = "/tmp/neo_regression_clipboard_XXXXXX";
    char* shimDir = mkdtemp(shimDirTemplate);
    assert(shimDir != NULL);
    static const char* shimTools[] = { "wl-copy", "wl-paste", "xclip", "xsel" };
    for (size_t index = 0; index < sizeof(shimTools) / sizeof(shimTools[0]); index++) {
        RegressionClipboardWriteShim(shimDir, shimTools[index]);
    }

    const char* currentPath = getenv("PATH");
    char* savedPath = currentPath ? strdup(currentPath) : NULL;
    char newPath[4096];
    snprintf(newPath, sizeof(newPath), "%s:%s", shimDir, savedPath ? savedPath : "");
    assert(setenv("PATH", newPath, 1) == 0);

    // 128 KiB: larger than the kernel pipe buffer (64 KiB), so the fwrite to
    // the dead shim cannot complete eagerly -- it blocks with the reader
    // gone, and an unguarded write would die of SIGPIPE.
    const size_t payloadSize = 128 * 1024;
    char* payload = malloc(payloadSize + 1);
    assert(payload != NULL);
    memset(payload, 'a', payloadSize);
    payload[payloadSize] = '\0';

    Clipboard_Write(payload); // must survive the dead consumer

    // All system paste tools fail too, so Clipboard_Read falls back to the
    // internal clipboard, which must still hold the payload intact.
    Slice clipboardText = Clipboard_Read();
    assert(clipboardText.size == payloadSize);
    assert(memcmp(clipboardText.data, payload, payloadSize) == 0);

    free(payload);
    Clipboard_Free();
    if (savedPath) {
        setenv("PATH", savedPath, 1);
        free(savedPath);
    } else {
        unsetenv("PATH");
    }
    for (size_t index = 0; index < sizeof(shimTools) / sizeof(shimTools[0]); index++) {
        RegressionClipboardRemoveShim(shimDir, shimTools[index]);
    }
    rmdir(shimDir);
}

// ---------------------------------------------------------------------------
// Suite regression_args: an over-long "+line[:col]" argument must not be silently
// truncated. Copied into a fixed char temp[128] with strncpy, a zero-padded
// "+000...05:3" (204 chars) leaves a surviving prefix that is all zeros with
// no colon, so the parse "succeeds" with line 0 / column 0 -- the jump is
// silently wrong instead of an error. An over-long +arg must be EITHER
// rejected (CliOptions_Parse returns false) OR parsed without truncation
// (line 5, column 3); the assertion accepts both.
// ---------------------------------------------------------------------------

TEST(regression_args, overlong_plus_jump_not_truncated)
{
    // "+" + 200 '0's + "5:3" -> true line 5, column 3.
    char jumpArgument[256];
    jumpArgument[0] = '+';
    memset(jumpArgument + 1, '0', 200);
    memcpy(jumpArgument + 201, "5:3", 4); // includes the NUL terminator

    char* argv[] = { "neo", jumpArgument, "file.txt" };
    CliOptions options;
    bool parsed = CliOptions_Parse(&options, 3, argv);

    assert(!parsed
        || (options.fileCount == 1 && options.fileLines[0] == 5 && options.fileColumns[0] == 3));

    CliOptions_Free(&options);
}
