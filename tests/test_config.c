#include "../src/neo.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

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
