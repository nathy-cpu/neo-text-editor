#include "../src/neo.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_get_syntax_color_defaults(void)
{
    assert(GetSyntaxColor(NULL, HIGHLIGHT_NORMAL) == NULL);
    assert(strcmp(GetSyntaxColor(NULL, HIGHLIGHT_NUMBER), "35") == 0);
    assert(strcmp(GetSyntaxColor(NULL, HIGHLIGHT_MATCH), "34") == 0);
    assert(strcmp(GetSyntaxColor(NULL, HIGHLIGHT_STRING), "38;5;208") == 0);
    assert(strcmp(GetSyntaxColor(NULL, HIGHLIGHT_CHARACTER), "33") == 0);
    assert(strcmp(GetSyntaxColor(NULL, HIGHLIGHT_COMMENT), "90") == 0);
    assert(strcmp(GetSyntaxColor(NULL, HIGHLIGHT_KEYWORD), "34") == 0);
    assert(strcmp(GetSyntaxColor(NULL, HIGHLIGHT_TYPE), "32") == 0);
    assert(strcmp(GetSyntaxColor(NULL, HIGHLIGHT_SYMBOL), "36") == 0);
}

static void test_get_syntax_color_config_override(void)
{
    Config config;
    memset(&config, 0, sizeof(config));
    config.syntaxColors[HIGHLIGHT_KEYWORD] = "99";

    // Overridden entry wins over the hardcoded default.
    assert(strcmp(GetSyntaxColor(&config, HIGHLIGHT_KEYWORD), "99") == 0);
    // Entries left NULL in the config still fall back to the hardcoded default.
    assert(strcmp(GetSyntaxColor(&config, HIGHLIGHT_NUMBER), "35") == 0);
}

static void test_syntax_highlight_c_file(void)
{
    Editor editor;
    Editor_Init(&editor);
    Editor_AddTab(&editor, "foo.c");
    Tab* tab = Array_Get(&editor.tabs, Tab*, editor.activeTabIndex);

    assert(tab->syntax != NULL);
    assert(strcmp(tab->syntax->fileType, "C") == 0);

    const char* code = "int x = 42; // comment";
    Line* line = Buffer_GetLine(tab->buffer, 0);
    Line_InsertText(line, 0, code, strlen(code));
    Tab_UpdateSyntax(tab);

    line = Buffer_GetLine(tab->buffer, 0);
    Slice stylesSlice = Array_ToSlice(&line->styles);
    char* styles = (char*)stylesSlice.data;
    assert(stylesSlice.size == strlen(code));

    assert(styles[0] == HIGHLIGHT_TYPE && styles[1] == HIGHLIGHT_TYPE && styles[2] == HIGHLIGHT_TYPE); // "int"
    assert(styles[4] == HIGHLIGHT_NORMAL); // "x"
    assert(styles[6] == HIGHLIGHT_SYMBOL); // "="
    assert(styles[8] == HIGHLIGHT_NUMBER && styles[9] == HIGHLIGHT_NUMBER); // "42"
    assert(styles[10] == HIGHLIGHT_SYMBOL); // ";"
    for (size_t i = 12; i < strlen(code); i++) {
        assert(styles[i] == HIGHLIGHT_COMMENT); // "// comment"
    }

    Editor_Free(&editor);
}

static void test_syntax_highlight_strings_and_chars(void)
{
    Tab tab;
    Tab_Init(&tab);

    Syntax syntax;
    memset(&syntax, 0, sizeof(syntax));
    tab.syntax = &syntax;

    // Plain string and character literal, space-separated.
    const char* text0 = "\"hi\" 'x'";
    // A string containing an escaped quote must not close the string early.
    const char* text1 = "\"a\\\"b\"";

    // Insert both lines' content (with the newline between them) in a single
    // edit, so Tab_UpdateSyntax's incremental rebuild highlights both lines in
    // one pass instead of needing a separate call per edit.
    Line* line0 = Buffer_GetLine(tab.buffer, 0);
    char fullText[64];
    int fullLength = snprintf(fullText, sizeof(fullText), "%s\n%s", text0, text1);
    Line_InsertText(line0, 0, fullText, (size_t)fullLength);
    Tab_UpdateSyntax(&tab);

    line0 = Buffer_GetLine(tab.buffer, 0);
    Slice styles0Slice = Array_ToSlice(&line0->styles);
    char* styles0 = (char*)styles0Slice.data;
    assert(styles0Slice.size == strlen(text0));
    for (size_t i = 0; i <= 3; i++) {
        assert(styles0[i] == HIGHLIGHT_STRING); // "hi"
    }
    assert(styles0[4] == HIGHLIGHT_NORMAL); // space
    for (size_t i = 5; i <= 7; i++) {
        assert(styles0[i] == HIGHLIGHT_CHARACTER); // 'x'
    }

    Line* line1 = Buffer_GetLine(tab.buffer, 1);
    Slice styles1Slice = Array_ToSlice(&line1->styles);
    char* styles1 = (char*)styles1Slice.data;
    assert(styles1Slice.size == strlen(text1));
    for (size_t i = 0; i < strlen(text1); i++) {
        assert(styles1[i] == HIGHLIGHT_STRING); // whole escaped literal, including the escaped quote
    }

    Tab_Free(&tab);
}

static void test_syntax_highlight_multiline_comment(void)
{
    Tab tab;
    Tab_Init(&tab);

    Syntax syntax;
    memset(&syntax, 0, sizeof(syntax));
    syntax.multiLineCommentStart = "/*";
    syntax.multiLineCommentEnd = "*/";
    tab.syntax = &syntax;

    const char* text0 = "int x; /* start";
    const char* text1 = "end */ int y;";

    // Insert both lines' content (with the newline between them) in a single
    // edit, so Tab_UpdateSyntax's incremental rebuild highlights both lines --
    // and threads the multi-line-comment state between them -- in one pass.
    Line* line0 = Buffer_GetLine(tab.buffer, 0);
    char fullText[64];
    int fullLength = snprintf(fullText, sizeof(fullText), "%s\n%s", text0, text1);
    Line_InsertText(line0, 0, fullText, (size_t)fullLength);
    Tab_UpdateSyntax(&tab);

    line0 = Buffer_GetLine(tab.buffer, 0);
    assert(line0->commentStateOutValid == true);
    assert(line0->commentStateOut == true); // Line 0 ends still inside the comment.
    Slice styles0Slice = Array_ToSlice(&line0->styles);
    char* styles0 = (char*)styles0Slice.data;
    for (size_t i = 7; i < strlen(text0); i++) {
        assert(styles0[i] == HIGHLIGHT_COMMENT); // "/* start"
    }

    Line* line1 = Buffer_GetLine(tab.buffer, 1);
    assert(line1->commentStateOut == false); // Comment closes partway through line 1.
    Slice styles1Slice = Array_ToSlice(&line1->styles);
    char* styles1 = (char*)styles1Slice.data;
    for (size_t i = 0; i <= 5; i++) {
        assert(styles1[i] == HIGHLIGHT_COMMENT); // "end */"
    }
    for (size_t i = 6; i < strlen(text1); i++) {
        assert(styles1[i] != HIGHLIGHT_COMMENT); // trailing "int y;" is no longer commented out
    }

    Tab_Free(&tab);
}

static void test_syntax_no_match_leaves_syntax_null(void)
{
    Editor editor;
    Editor_Init(&editor);
    Editor_AddTab(&editor, "foo.xyz");
    Tab* tab = Array_Get(&editor.tabs, Tab*, editor.activeTabIndex);

    assert(tab->syntax == NULL);

    Line* line = Buffer_GetLine(tab->buffer, 0);
    assert(line->styles.data == NULL); // Never allocated -- Tab_UpdateSyntax is a no-op without a syntax.

    Editor_Free(&editor);
}
