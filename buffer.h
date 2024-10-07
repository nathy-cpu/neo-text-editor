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

#define SCREEN_BUFFER_INIT (ScreenBuffer){NULL,0}

void ScreenBuffer_append(ScreenBuffer* sbuf, const char* string, size_t size);

void ScreenBuffer_free(ScreenBuffer* sbuf);

/******* syntax highlighting ********/

enum HighlightFlag
{
    HIGHLIGHT_NORMAL          =    0,
    HIGHLIGHT_NUMBER          =    1,
    HIGHLIGHT_STRING          =    2,
    HIGHLIGHT_CHARACTER       =    3,
    HIGHLIGHT_SELECTION       =    4,
    HIGHLIGHT_COMMENT         =    5,
    HIGHLIGHT_KEYWORD         =    6,
    HIGHLIGHT_TYPE            =    7,
    HIGHLIGHT_PREPROCESSOR    =    8,
    HIGHLIGHT_SEPARATOR       =    9,
    HIGHLIGHT_ALL             =    10
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
    char**   preprocessorStart;
    int      flag;

} Syntax;

#define  SYNTAXINIT (Syntax){NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,0}

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

void    TextRow_updateRender(TextRow* row);

void    TextRow_updateSyntax(TextRow* row, const Syntax syn);

void    TextRow_free(TextRow* row);

void    TextRow_insertChar(TextRow* row, size_t index, short int input, const Syntax syn);

void    TextRow_appendString(TextRow* row, char* str, size_t size, const Syntax syn);

void    TextRow_deleteChar(TextRow* row, size_t index, const Syntax syn);

size_t  TextRow_getRenderX(TextRow* row, size_t cursorX);

size_t  TextRow_getCursorX(TextRow* row, size_t renderX);

bool    previousWhiteSpace(const TextRow* row, size_t index);

/******* text buffer structure to render and edit a file from ********/

typedef struct
{
    Syntax      syntax;
    TextRow*    textRow;
    size_t      numberofTextRows;

} TextBuffer;

void    TextBuffer_insertTextRow(TextBuffer* tbuf, size_t index, const char* str, size_t size);

void    TextBuffer_deleteTextRow(TextBuffer* tbuf,size_t index);

void    TextBuffer_updateSyntax(TextBuffer* tbuf, size_t offset);

char*   TextBuffer_toString(TextBuffer* tbuf, size_t* bufferSize);

#endif // BUFFER_H
