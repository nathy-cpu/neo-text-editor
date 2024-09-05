#ifndef BUFFER_H
#define BUFFER_H

#include "dependencies.h"
#include "terminal.h"

/******* screen buffer structure to write to terminal from ********/

typedef struct
{
    char*     string;
    size_t    size;

} ScreenBuffer;

#define SCREEN_BUFFER_INIT { NULL, 0 }

void ScreenBufferAppend(ScreenBuffer* sbuf, const char* string, size_t size);

void ScreenBufferFree(ScreenBuffer* sbuf);

/******* syntax highlighting ********/

enum HighlightFlag
{
    HIGHLIGHT_NORMAL          =    0,
    HIGHLIGHT_NUMBER          =    1,
    HIGHLIGHT_STRING          =    2,
    HIGHLIGHT_MATCH           =    4,
    HIGHLIGHT_CHARACTER       =    8,
    HIGHLIGHT_COMMENT         =    16,
    HIGHLIGHT_KEYWORD         =    32,
    HIGHLIGHT_SELECTION       =    64,
    HIGHLIGHT_TYPE            =    128,
    HIGHLIGHT_PREPROCESSOR    =    256,
    HIGHLIGHT_SEPARATOR       =    512,
    HIGHLIGHT_ALL             =    1024
};

typedef struct
{
    char*    fileType;
    char**   fileMatch;
    char**   keywords;
    char**   types;
    char*    singleLineCommentStarter;
    char*    multilineCommentStart;
    char*    multilineCommentEnd;
    int      flag;

} Syntax;

char*   getSyntaxColor(int highlight);

bool    isSeparator(int character);

/******* text row struct to organize and operate of each row of text in a file ********/

typedef struct
{
    char*             text;
    size_t            textSize;
    char*             render;
    size_t            renderSize;
    unsigned char*    highlight;
    size_t            index;
    bool              openComment;

} TextRow;

void    TextRowUpdateRender(TextRow* row);

void    TextRowUpdateSyntax(TextRow* row, Syntax* syn);

void    TextRowFree(TextRow* row);

void    TextRowInsertChar(TextRow* row, size_t index, short int input, Syntax* syn);

void    TextRowAppendString(TextRow* row, char* str, size_t size, Syntax* syn);

void    TextRowDeleteChar(TextRow* row, size_t index, Syntax* syn);

size_t  TextRowGetRenderX(TextRow* row, size_t cursorX);

size_t  TextRowGetCursorX(TextRow* row, size_t renderX);

/******* text buffer structure to render and edit a file from ********/

typedef struct
{
    Syntax*     syntax;
    TextRow*    textRow;
    size_t      numberofTextRows;

} TextBuffer;

void    TextBufferInsertTextRow(TextBuffer* tbuf, size_t index, const char* str, size_t size);

void    TextBufferDeleteTextRow(TextBuffer* tbuf,size_t index);

char*   TextBufferToString(TextBuffer* tbuf, size_t* bufferSize);

#endif // BUFFER_H
