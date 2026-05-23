#include "../src/neo.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static void test_buffer_basic(void)
{
    Buffer* buffer = Buffer_New();
    assert(buffer != NULL);
    assert(Buffer_GetLineCount(buffer) == 1);
    
    Buffer_InsertChar(buffer, 0, 0, 'a');
    Buffer_InsertChar(buffer, 0, 1, 'b');
    Buffer_InsertChar(buffer, 0, 2, 'c');
    
    Line* line = Buffer_GetLine(buffer, 0);
    assert(Line_Length(line) == 3);
    
    Buffer_SplitLine(buffer, 0, 1); // "a", "bc"
    assert(Buffer_GetLineCount(buffer) == 2);
    
    line = Buffer_GetLine(buffer, 0);
    assert(Line_Length(line) == 1);
    
    line = Buffer_GetLine(buffer, 1);
    assert(Line_Length(line) == 2);
    
    Buffer_JoinLine(buffer, 0); // "abc"
    assert(Buffer_GetLineCount(buffer) == 1);
    
    line = Buffer_GetLine(buffer, 0);
    assert(Line_Length(line) == 3);
    
    Buffer_Free(buffer);
}

static void test_buffer_lines(void)
{
    Buffer* buffer = Buffer_New();
    
    Buffer_InsertLine(buffer, 1);
    Buffer_InsertLine(buffer, 2);
    assert(Buffer_GetLineCount(buffer) == 3);
    
    Buffer_InsertChar(buffer, 0, 0, '1');
    Buffer_InsertChar(buffer, 1, 0, '2');
    Buffer_InsertChar(buffer, 2, 0, '3');
    
    Buffer_DeleteLine(buffer, 1);
    assert(Buffer_GetLineCount(buffer) == 2);
    
    Line* line = Buffer_GetLine(buffer, 1);
    Slice text = Line_GetText(line);
    assert(text.size == 1 && ((const char*)text.data)[0] == '3');
    
    Buffer_Free(buffer);
}
