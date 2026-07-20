#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "../src/features/editor.h"
#include "../src/features/config.h"
#include "../src/features/syntax.h"
#include "../src/utils/array.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

static void test_config_defaults(void)
{
    Editor editor;
    Editor_Init(&editor);

    assert(editor.config.tabSize == 4);
    assert(editor.config.showLineNumbers == true);
    assert(editor.config.wrapLines == true);
    assert(editor.config.syntaxEnabled == true);
    assert(editor.config.statusTimeout == 5);

    // Verify keybindings
    assert(editor.config.keySave == 19); // ctrl-s
    assert(editor.config.keyQuit == 17); // ctrl-q

    // Verify default colors are present
    assert(editor.config.syntaxColors[HIGHLIGHT_KEYWORD] != NULL);
    assert(strcmp(editor.config.syntaxColors[HIGHLIGHT_KEYWORD], "34") == 0);

    // Verify default syntax database contains C and C++
    size_t count = Array_Size(&editor.config.syntaxDatabase);
    assert(count >= 2);

    Syntax* cSyntax = (Syntax*)Array_At(&editor.config.syntaxDatabase, 0);
    assert(strcmp(cSyntax->fileType, "C") == 0);

    Editor_Free(&editor);
}

static void test_config_defaults_full(void)
{
    Editor editor;
    Editor_Init(&editor);
    Config* c = &editor.config;

    // Logging defaults.
    assert(strcmp(c->logFile, "neo.log") == 0);
    assert(strcmp(c->logLevelStr, "INFO") == 0);
    assert(c->logToFile == false);
    assert(c->logToUi == true);
    assert(c->logMaxMessages == 1000);
    assert(c->undoLimit == 1000);

    // Every default syntax color, not just HIGHLIGHT_KEYWORD.
    assert(c->syntaxColors[HIGHLIGHT_NORMAL] == NULL);
    assert(strcmp(c->syntaxColors[HIGHLIGHT_NUMBER], "35") == 0);
    assert(strcmp(c->syntaxColors[HIGHLIGHT_MATCH], "34") == 0);
    assert(strcmp(c->syntaxColors[HIGHLIGHT_STRING], "38;5;208") == 0);
    assert(strcmp(c->syntaxColors[HIGHLIGHT_CHARACTER], "33") == 0);
    assert(strcmp(c->syntaxColors[HIGHLIGHT_COMMENT], "90") == 0);
    assert(strcmp(c->syntaxColors[HIGHLIGHT_KEYWORD], "34") == 0);
    assert(strcmp(c->syntaxColors[HIGHLIGHT_TYPE], "32") == 0);
    assert(strcmp(c->syntaxColors[HIGHLIGHT_SYMBOL], "36") == 0);

    // All three default languages, not just C.
    assert(Array_Size(&c->syntaxDatabase) == 3);
    Syntax* cSyntax = (Syntax*)Array_At(&c->syntaxDatabase, 0);
    Syntax* cppSyntax = (Syntax*)Array_At(&c->syntaxDatabase, 1);
    Syntax* logSyntax = (Syntax*)Array_At(&c->syntaxDatabase, 2);
    assert(strcmp(cSyntax->fileType, "C") == 0);
    assert(strcmp(cppSyntax->fileType, "C++") == 0);
    assert(strcmp(logSyntax->fileType, "Log") == 0);

    // Every default keybinding.
    assert(c->keySave == CTRL_KEY('s'));
    assert(c->keyQuit == CTRL_KEY('q'));
    assert(c->keyNewTab == CTRL_KEY('t'));
    assert(c->keyCloseTab == CTRL_KEY('w'));
    assert(c->keyExplorer == CTRL_KEY('e'));
    assert(c->keyNextTab == CTRL_KEY('n'));
    assert(c->keyPrevTab == CTRL_KEY('p'));
    assert(c->keySaveAs == ALT_S);
    assert(c->keyLogs == CTRL_KEY('l'));
    assert(c->keyToggleFold == CTRL_KEY('f'));
    assert(c->keyToggleAllFolds == ALT_F);

    Editor_Free(&editor);
}

static void test_config_lua_load(void)
{
    Editor editor;
    Editor_Init(&editor);

    // Load from our sample config.lua
    bool isConfigLoaded = Editor_LoadConfig(&editor, "config.lua");
    assert(isConfigLoaded == true);

    // Verify our changes from config.lua are active
    assert(editor.config.tabSize == 4);
    assert(editor.config.showLineNumbers == true);
    assert(editor.config.wrapLines == false); // overridden from default true
    assert(editor.config.syntaxEnabled == true);
    assert(editor.config.statusTimeout == 3); // overridden from default 5

    // Verify custom color for keyword
    assert(strcmp(editor.config.syntaxColors[HIGHLIGHT_KEYWORD], "35") == 0); // magenta, overridden

    // Verify new syntax rule for Lua was added/replaced
    size_t count = Array_Size(&editor.config.syntaxDatabase);
    bool isLuaLanguageFound = false;
    for (size_t index = 0; index < count; index++) {
        Syntax* syntax = (Syntax*)Array_At(&editor.config.syntaxDatabase, index);
        if (strcmp(syntax->fileType, "Lua") == 0) {
            isLuaLanguageFound = true;
            assert(syntax->fileMatch != NULL);
            assert(strcmp(syntax->fileMatch[0], ".lua") == 0);
            assert(strcmp(syntax->singleLineCommentStart, "--") == 0);
            break;
        }
    }
    assert(isLuaLanguageFound == true);

    Editor_Free(&editor);
}

static void test_config_lua_load_full(void)
{
    Editor editor;
    Editor_Init(&editor);
    assert(Editor_LoadConfig(&editor, "config.lua") == true);
    Config* c = &editor.config;

    // Logging fields from config.lua, previously loaded but never asserted.
    assert(strcmp(c->logFile, "neo.log") == 0);
    assert(strcmp(c->logLevelStr, "INFO") == 0);
    assert(c->logToFile == true);
    assert(c->logToUi == true);
    assert(c->logMaxMessages == 1000);

    // The other overridden colors, not just "keyword".
    assert(strcmp(c->syntaxColors[HIGHLIGHT_TYPE], "36") == 0);
    assert(strcmp(c->syntaxColors[HIGHLIGHT_STRING], "32") == 0);
    assert(strcmp(c->syntaxColors[HIGHLIGHT_COMMENT], "90") == 0);

    // Every keybinding config.lua overrides.
    assert(c->keySave == CTRL_KEY('s'));
    assert(c->keyQuit == CTRL_KEY('q'));
    assert(c->keyNewTab == CTRL_KEY('t'));
    assert(c->keyCloseTab == CTRL_KEY('w'));
    assert(c->keyNextTab == CTRL_KEY('n'));
    assert(c->keyPrevTab == CTRL_KEY('p'));
    assert(c->keySaveAs == ALT_S);
    assert(c->keyLogs == CTRL_KEY('l'));
    assert(c->keyToggleFold == CTRL_KEY('f'));
    assert(c->keyToggleAllFolds == ALT_F);
    // config.lua's keybindings table has no "explorer" entry, so it must keep
    // the default rather than being zeroed/clobbered.
    assert(c->keyExplorer == CTRL_KEY('e'));

    // The Lua language entry's array fields, not just fileMatch[0]/singleLineCommentStart.
    bool foundLua = false;
    for (size_t i = 0; i < Array_Size(&c->syntaxDatabase); i++) {
        Syntax* syntax = (Syntax*)Array_At(&c->syntaxDatabase, i);
        if (strcmp(syntax->fileType, "Lua") == 0) {
            foundLua = true;
            assert(syntax->keywords != NULL && strcmp(syntax->keywords[0], "and") == 0);
            assert(syntax->types != NULL && strcmp(syntax->types[0], "io") == 0);
            assert(strcmp(syntax->multiLineCommentStart, "--[[") == 0);
            assert(strcmp(syntax->multiLineCommentEnd, "]]") == 0);
            break;
        }
    }
    assert(foundLua);

    Editor_Free(&editor);
}

static void test_config_load_missing_file_fails(void)
{
    char* originalHome = getenv("HOME");
    char* homeCopy = originalHome ? strdup(originalHome) : NULL;

    // Point HOME at a directory with no .config/neo/config.lua, so the
    // home-directory fallback deterministically also misses.
    mkdir("fake_home_for_config_test", 0755);
    setenv("HOME", "fake_home_for_config_test", 1);

    Editor editor;
    Editor_Init(&editor);
    assert(Editor_LoadConfig(&editor, "definitely_does_not_exist_anywhere.lua") == false);
    Editor_Free(&editor);

    // No HOME at all must also fail cleanly, not crash.
    unsetenv("HOME");
    Editor editor2;
    Editor_Init(&editor2);
    assert(Editor_LoadConfig(&editor2, "definitely_does_not_exist_anywhere.lua") == false);
    Editor_Free(&editor2);

    if (homeCopy) {
        setenv("HOME", homeCopy, 1);
        free(homeCopy);
    } else {
        unsetenv("HOME");
    }
    rmdir("fake_home_for_config_test");
}

static void test_config_load_malformed_lua_fails(void)
{
    const char* path = "malformed_test_config.lua";
    FILE* f = fopen(path, "w");
    assert(f != NULL);
    fputs("this is not ) valid ( lua syntax !!!\n", f);
    fclose(f);

    Editor editor;
    Editor_Init(&editor);
    assert(Editor_LoadConfig(&editor, path) == false);
    Editor_Free(&editor);

    remove(path);
}

static void test_config_load_language_replace_and_discard(void)
{
    const char* path = "language_replace_test_config.lua";
    FILE* f = fopen(path, "w");
    assert(f != NULL);
    fputs("languages = {\n"
          "  { name = \"C\", extensions = { \".c\" }, single_line_comment = \"#\" },\n"
          "  { extensions = { \".weird\" } },\n" // missing "name" -> must be discarded, not appended
          "}\n",
        f);
    fclose(f);

    Editor editor;
    Editor_Init(&editor);
    size_t countBefore = Array_Size(&editor.config.syntaxDatabase);
    assert(Editor_LoadConfig(&editor, path) == true);

    // The malformed entry (no "name") must be discarded, and the "C" entry
    // replaced in place rather than appended as a duplicate.
    assert(Array_Size(&editor.config.syntaxDatabase) == countBefore);

    bool foundReplacedC = false;
    for (size_t i = 0; i < Array_Size(&editor.config.syntaxDatabase); i++) {
        Syntax* syntax = (Syntax*)Array_At(&editor.config.syntaxDatabase, i);
        if (strcmp(syntax->fileType, "C") == 0) {
            assert(!foundReplacedC && "C must appear exactly once, not duplicated");
            foundReplacedC = true;
            assert(strcmp(syntax->singleLineCommentStart, "#") == 0);
        }
    }
    assert(foundReplacedC);

    Editor_Free(&editor);
    remove(path);
}
