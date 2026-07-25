// Regression tests for syntax-highlighting over-reads and state corruption.
//
// The scenarios are built around Line_GetText's scratch semantics: it
// returns a Slice into a SHARED STATIC scratch buffer (src/core/line.c) that
// is NOT NUL-terminated and is sized to the largest request seen so far.
// Bytes past the current line's length are STALE contents of previously
// fetched (longer) lines, and when the current line is the longest fetch so
// far the buffer is EXACTLY sized, so any lookahead past the slice length
// reads past the heap allocation. UpdateLineSyntax (src/features/syntax.c)
// does strncmp lookaheads for comment markers and keyword/type matches plus
// IsSeparator(text[i + keywordLength]) boundary checks; each must stay
// bounded to the slice length -- a stale-byte false match also memsets
// styles PAST the styles array's logical size.

#include "../src/features/editor.h"
#include "../src/features/tab.h"
#include "../src/features/syntax.h"
#include "../src/features/config.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Inserts `text` (may contain newlines) at the top of the tab's buffer in a
// single edit, then runs highlighting over the whole buffer. Tab_UpdateSyntax
// walks lines top-down, so Line_GetText is called in line order: the scratch
// buffer a given line sees was primed by the lines ABOVE it.
static void RegressionSyntaxInsertAndHighlight(Tab* tab, const char* text)
{
    Line* firstLine = Buffer_GetLine(tab->buffer, 0);
    assert(firstLine != NULL);
    Buffer_InsertText(tab->buffer, firstLine->offset, text, strlen(text));
    Tab_UpdateSyntax(tab, SIZE_MAX);
}

static Slice RegressionSyntaxLineStyles(Tab* tab, size_t lineIndex)
{
    Line* line = Buffer_GetLine(tab->buffer, lineIndex);
    assert(line != NULL);
    assert(line->styles != NULL);
    return Array_ToSlice(line->styles);
}

// A 2-char line "in" processed right after the longer line "int foo;" sees
// stale scratch bytes "t foo;" after its own text: an unbounded type lookup
// strncmp(&text[0], "int", 3) would falsely match (stale 't'), the boundary
// check IsSeparator(text[3]) would read the stale ' ', and the resulting
// memset(&styles[0], HIGHLIGHT_TYPE, 3) would write 1 byte past the 2-byte
// styles array. "in" must highlight as an ordinary identifier: all NORMAL.
TEST(regression_syntax, stale_scratch_no_false_type_match)
{
    Editor editor;
    Editor_Init(&editor);
    Editor_AddTab(&editor, "foo.c"); // default C rules ("int" is a type)
    Tab* tab = Array_Get(&editor.tabs, Tab*, editor.activeTabIndex);
    assert(tab->syntax != NULL);
    assert(strcmp(tab->syntax->fileType, "C") == 0);

    // Line 0 ("int foo;", 8 bytes) is ABOVE line 1 ("in", 2 bytes): the
    // top-down highlight pass primes the scratch buffer with line 0's text
    // immediately before line 1 is fetched.
    RegressionSyntaxInsertAndHighlight(tab, "int foo;\nin");

    Slice styles = RegressionSyntaxLineStyles(tab, 1);
    char* styleBytes = (char*)styles.data;
    assert(styles.size == 2);
    assert(styleBytes[0] == HIGHLIGHT_NORMAL);
    assert(styleBytes[1] == HIGHLIGHT_NORMAL);

    Editor_Free(&editor);
}

// A line ending in '/' that is the LONGEST line fetched so far leaves the
// scratch buffer exactly line-sized, so an unbounded single-line-comment
// lookahead strncmp(&text[5], "//", 2) would read 1 byte past the heap
// allocation. Lines are arranged in increasing length so no earlier, longer
// fetch pads the scratch. Highlighting must complete cleanly and the lone
// trailing '/' is not a comment.
TEST(regression_syntax, line_end_lookahead_no_overread)
{
    Tab tab;
    Tab_Init(&tab);

    Syntax syntax;
    memset(&syntax, 0, sizeof(syntax));
    syntax.singleLineCommentStart = "//";
    tab.syntax = &syntax;

    // "ab" (2 bytes) is fetched first, then "abcde/" (6 bytes) -- the longest
    // request so far, so scratch is (re)allocated to exactly 6 bytes and the
    // lookahead at the trailing '/' reads scratch[6].
    RegressionSyntaxInsertAndHighlight(&tab, "ab\nabcde/");

    Slice styles = RegressionSyntaxLineStyles(&tab, 1);
    char* styleBytes = (char*)styles.data;
    assert(styles.size == 6);
    assert(styleBytes[5] != HIGHLIGHT_COMMENT); // a lone '/' is not a comment

    Tab_Free(&tab);
}

// A stale-byte false match of "*/" at a line end would flip
// inMultiLineComment off mid-file, mis-highlighting everything below.
//
// Stale-byte indexing:
//   line 0 "/* long opener line" (19 bytes) opens the block comment and sizes
//          the scratch buffer to 19, so the shorter lines below never trigger
//          an exact-size overread of their own;
//   line 1 "aa////////" (10 bytes) is comment interior; after it is fetched,
//          scratch[0..9] = "aa////////" (indices 2..9 are all '/');
//   line 2 "bbbb*" (5 bytes) is comment interior; after its memcpy,
//          scratch[0..4] = "bbbb*" and scratch[5] is STALE = line 1's byte at
//          index 5 = '/'. An unbounded in-comment scan at i == 4 sees
//          text[4] == '*' and stale text[5] == '/', so strncmp(&text[4],
//          "*/", 2) matches: the memset writes styles[5] (1 past the 5-byte
//          styles array) and the comment state flips to closed;
//   line 3 "cccc" is still inside the never-closed comment.
// Line 2 must end inside the comment and line 3 must be all COMMENT.
TEST(regression_syntax, multiline_state_not_corrupted_by_stale_bytes)
{
    Tab tab;
    Tab_Init(&tab);

    Syntax syntax;
    memset(&syntax, 0, sizeof(syntax));
    syntax.multiLineCommentStart = "/*";
    syntax.multiLineCommentEnd = "*/";
    tab.syntax = &syntax;

    RegressionSyntaxInsertAndHighlight(&tab, "/* long opener line\naa////////\nbbbb*\ncccc");

    Line* line2 = Buffer_GetLine(tab.buffer, 2);
    assert(line2 != NULL);
    assert(line2->commentStateOutValid == true);
    assert(line2->commentStateOut == true); // comment never actually closes

    Slice styles = RegressionSyntaxLineStyles(&tab, 3);
    char* styleBytes = (char*)styles.data;
    assert(styles.size == 4);
    for (size_t i = 0; i < styles.size; i++) {
        assert(styleBytes[i] == HIGHLIGHT_COMMENT); // "cccc" is inside the comment
    }

    Tab_Free(&tab);
}

// For a language whose single-line comment marker is a prefix of its
// multi-line one (Lua: "--" vs "--[["), the multi-line start must be checked
// first. If the single-line marker wins, a block comment can never open:
// "--[[ start" is swallowed as a single-line comment and the block interior
// highlights as code. The interior line must be all COMMENT.
TEST(regression_syntax, lua_block_comment_opens)
{
    Tab tab;
    Tab_Init(&tab);

    Syntax syntax;
    memset(&syntax, 0, sizeof(syntax));
    syntax.singleLineCommentStart = "--";
    syntax.multiLineCommentStart = "--[[";
    syntax.multiLineCommentEnd = "]]";
    tab.syntax = &syntax;

    RegressionSyntaxInsertAndHighlight(&tab, "--[[ start\nlocal x = 1\n]]");

    Line* line0 = Buffer_GetLine(tab.buffer, 0);
    assert(line0 != NULL);
    assert(line0->commentStateOutValid == true);
    assert(line0->commentStateOut == true); // block comment is open after line 0

    Slice styles = RegressionSyntaxLineStyles(&tab, 1);
    char* styleBytes = (char*)styles.data;
    assert(styles.size == strlen("local x = 1"));
    for (size_t i = 0; i < styles.size; i++) {
        assert(styleBytes[i] == HIGHLIGHT_COMMENT); // whole interior line is commented out
    }

    Tab_Free(&tab);
}

// previousSeparator must be updated on string open/close; left untouched, it
// carries over from the character BEFORE the opening quote. In `x="s"if` the
// '=' before the string would leave previousSeparator == true, falsely
// keyword-highlighting the "if" glued directly to the closing quote. A token
// glued to a closing quote is not keyword-eligible: "if" stays NORMAL.
TEST(regression_syntax, keyword_after_string_close)
{
    Editor editor;
    Editor_Init(&editor);
    Editor_AddTab(&editor, "foo.c"); // default C rules ("if" is a keyword)
    Tab* tab = Array_Get(&editor.tabs, Tab*, editor.activeTabIndex);
    assert(tab->syntax != NULL);

    RegressionSyntaxInsertAndHighlight(tab, "x=\"s\"if");

    Slice styles = RegressionSyntaxLineStyles(tab, 0);
    char* styleBytes = (char*)styles.data;
    assert(styles.size == 7);
    // indices: 0'x' 1'=' 2'"' 3's' 4'"' 5'i' 6'f'
    assert(styleBytes[5] == HIGHLIGHT_NORMAL);
    assert(styleBytes[6] == HIGHLIGHT_NORMAL);

    Editor_Free(&editor);
}
