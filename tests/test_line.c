#include "../src/core/line.h"
#include "../src/core/buffer.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

TEST(line, line_basic)
{
    Buffer* buffer = Buffer_New();
    
    Buffer_InsertChar(buffer, 0, 0, 'H');
    Buffer_InsertChar(buffer, 0, 1, 'i');
    
    Line* line = Buffer_GetLine(buffer, 0);
    assert(line != NULL);
    assert(Line_Length(line) == 2);
    
    Slice s = Line_GetText(line, buffer);
    assert(s.size == 2);
    assert(memcmp(s.data, "Hi", 2) == 0);
    
    Buffer_InsertText(buffer, 2, " there!", 7);
    line = Buffer_GetLine(buffer, 0);
    assert(Line_Length(line) == 9);
    
    s = Line_GetText(line, buffer);
    assert(s.size == 9);
    assert(memcmp(s.data, "Hi there!", 9) == 0);
    
    Buffer_DeleteChar(buffer, 0, 0); // "i there!"
    line = Buffer_GetLine(buffer, 0);
    assert(Line_Length(line) == 8);
    
    Buffer_DeleteRange(buffer, 0, 2); // "there!"
    line = Buffer_GetLine(buffer, 0);
    assert(Line_Length(line) == 6);
    
    s = Line_GetText(line, buffer);
    assert(s.size == 6);
    assert(memcmp(s.data, "there!", 6) == 0);
    
    Buffer_Free(buffer);
}

TEST(line, line_get_char)
{
    Buffer* buffer = Buffer_New();
    Buffer_InsertText(buffer, 0, "hello", 5);

    Line* line = Buffer_GetLine(buffer, 0);
    assert(Line_GetChar(line, buffer, 0) == 'h');
    assert(Line_GetChar(line, buffer, 4) == 'o');

    Buffer_Free(buffer);
}

TEST(line, line_render_x)
{
    Buffer* buffer = Buffer_New();
    // "a\tbc\td"
    Buffer_InsertText(buffer, 0, "a\tbc\td", 6);
    
    Line* line = Buffer_GetLine(buffer, 0);
    
    // TAB_STOP is 4
    assert(Line_GetRenderX(line, buffer, 0, 4) == 0); // 'a'
    assert(Line_GetRenderX(line, buffer, 1, 4) == 1); // '\t'
    assert(Line_GetRenderX(line, buffer, 2, 4) == 4); // 'b'
    assert(Line_GetRenderX(line, buffer, 3, 4) == 5); // 'c'
    assert(Line_GetRenderX(line, buffer, 4, 4) == 6); // '\t'
    assert(Line_GetRenderX(line, buffer, 5, 4) == 8); // 'd'
    assert(Line_GetRenderX(line, buffer, 6, 4) == 9); // end

    Buffer_Free(buffer);
}

TEST(line, line_render_x_edges)
{
    // Empty line: no characters to walk, always renders at column 0.
    Buffer* buffer1 = Buffer_New();
    Line* empty = Buffer_GetLine(buffer1, 0);
    assert(Line_GetRenderX(empty, buffer1, 0, 4) == 0);
    assert(Line_GetRenderX(empty, buffer1, 5, 4) == 0); // cursorX past an empty line is still 0
    Buffer_Free(buffer1);

    // cursorX past the end of the text: the loop is bounded by text.size, so it
    // must stop there rather than reading (or rendering) past the real content.
    Buffer* buffer2 = Buffer_New();
    Buffer_InsertText(buffer2, 0, "\t\t", 2); // two tabs, TAB_STOP 4 -> columns 0..3, 4..7
    Line* line = Buffer_GetLine(buffer2, 0);
    assert(Line_GetRenderX(line, buffer2, 10, 4) == 8);
    Buffer_Free(buffer2);
}

static size_t NaiveRenderX(const char* text, size_t textLen, size_t cursorX, size_t tabSize)
{
    size_t rx = 0;
    for (size_t i = 0; i < cursorX && i < textLen; i++) {
        if (text[i] == '\t')
            rx += (tabSize - 1) - (rx % tabSize);
        rx++;
    }
    return rx;
}

// Regression test for the render-checkpoint index: for lines above the
// checkpoint threshold, Line_GetRenderX switches from an O(length) direct
// walk to a checkpoint-lookup + short local walk. Results must match an
// independently-computed naive walk exactly, including right at checkpoint
// stride boundaries where an off-by-one would be easy to introduce.
TEST(line, line_render_x_huge_line)
{
    Buffer* buffer = Buffer_New();

    const size_t length = 20000; // well above the render-checkpoint threshold
    char* content = malloc(length);
    assert(content);
    for (size_t i = 0; i < length; i++) {
        // Tab period (37) deliberately doesn't evenly divide the checkpoint
        // stride, so boundary columns don't all land on the same tab-stop phase.
        content[i] = (i % 37 == 0) ? '\t' : 'x';
    }
    Buffer_InsertText(buffer, 0, content, length);
    Line* line = Buffer_GetLine(buffer, 0);
    assert(Line_Length(line) == length);

    size_t tabSize = 4;
    size_t checkCols[]
        = { 0, 1, 36, 37, 4095, 4096, 4097, 8192, 8193, 12288, 16384, 19999, 20000, 25000 };
    for (size_t c = 0; c < sizeof(checkCols) / sizeof(checkCols[0]); c++) {
        size_t expected = NaiveRenderX(content, length, checkCols[c], tabSize);
        assert(Line_GetRenderX(line, buffer, checkCols[c], tabSize) == expected);
    }

    // Rebuilding for a different tab size must invalidate the cached
    // checkpoints rather than silently reusing stale render columns.
    size_t otherTabSize = 8;
    size_t expectedOther = NaiveRenderX(content, length, 4096, otherTabSize);
    assert(Line_GetRenderX(line, buffer, 4096, otherTabSize) == expectedOther);

    free(content);
    Buffer_Free(buffer);
}

// Regression test for Line_GetTextRange: sub-ranges must match substrings of
// the full Line_GetText output, including ranges that span multiple
// piece-table pieces (forced here via several separate inserts).
TEST(line, line_get_text_range)
{
    Buffer* buffer = Buffer_New();
    
    Buffer_InsertText(buffer, 0, "HelloWorld", 10);
    Buffer_InsertText(buffer, 5, ", ", 2); // "Hello, World"
    Buffer_InsertText(buffer, 12, "!!!", 3); // "Hello, World!!!"

    Line* line = Buffer_GetLine(buffer, 0);
    Slice full = Line_GetText(line, buffer);
    char fullData[64];
    assert(full.size <= sizeof(fullData));
    memcpy(fullData, full.data, full.size);
    assert(full.size == 15 && memcmp(fullData, "Hello, World!!!", 15) == 0);

    struct {
        size_t start, length;
    } ranges[] = {
        { 0, 5 }, // "Hello" -- first piece only
        { 0, 15 }, // whole line
        { 3, 4 }, // spans first insert + second insert
        { 5, 10 }, // spans second + third insert
        { 12, 3 }, // "!!!" -- third piece only
        { 10, 100 }, // length clamped to available bytes
        { 20, 5 }, // start past end -> empty
    };
    for (size_t i = 0; i < sizeof(ranges) / sizeof(ranges[0]); i++) {
        Slice range = Line_GetTextRange(line, buffer, ranges[i].start, ranges[i].length);
        size_t expectedLen = ranges[i].start >= full.size ? 0 : full.size - ranges[i].start;
        if (expectedLen > ranges[i].length) {
            expectedLen = ranges[i].length;
        }
        assert(range.size == expectedLen);
        if (expectedLen > 0) {
            assert(memcmp(range.data, fullData + ranges[i].start, expectedLen) == 0);
        }
    }

    Buffer_Free(buffer);
}

