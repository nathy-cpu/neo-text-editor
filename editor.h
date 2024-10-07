#ifndef EDITOR_H
#define EDITOR_H

#include "terminal.h"
#include "buffer.h"

typedef struct
{
    struct termios         originalTermios;
    short unsigned int     screenRows;
    short unsigned int     screenColumns;
    size_t                 cursorX;
    size_t                 cursorY;
    TextBuffer              textBuffer;
    size_t                 renderX;
    size_t                 rowOffset;
    size_t                 columnOffset;
    char*                  filename;
    char                   statusMessage[200];
    time_t                 statusMessageTime;
    bool                   isSaved;

} Editor;

/******* initializing the Editor ********/

void    disableRawMode(Editor *config);

void    enableRawMode(Editor *config);

void    Editor_kill(Editor *config);

int     Editor_getCursorPosition(Editor *config);

int     Editor_getWindowSize(Editor *config);

void    Editor_init(Editor *config);

void    Editor_setSyntaxHighlight(Editor *config, const Syntax HLDB[]);

/******* handling files to edit ********/

void    Editor_openFile(Editor *config, const char* filename, const Syntax HLDB[]);

void    Editor_saveToFile(Editor *config, const Syntax HLDB[]);

/******* Editor output ********/

void    Editor_scroll(Editor *config);

void    Editor_setStatusMessage(Editor *config, const char* fstring, ...);

void    Editor_drawRows(Editor *config, ScreenBuffer* sbuf);

void    Editor_drawStatusBar(Editor *config, ScreenBuffer* sbuf);

void    Editor_drawMessageBar(Editor *config, ScreenBuffer* sbuf);

void    Editor_refreshScreen(Editor *config);

/******* input ********/

char*   Editor_promptForInput(Editor *config, const char* prompt, void (*callBackFunction)(Editor*, char*, int));

void    Editor_moveCursor(Editor *config, short int key);

void    Editor_processKeypress(Editor *config, const Syntax HLDB[]);


/******* Editor operations ********/

void    Editor_insertChar(Editor *config, short int input);

void    Editor_insertNewLine(Editor *config);

void    Editor_deleteChar(Editor *config);

/******* text search ********/

void findCallBack(Editor* config, char* query, int key);

void Editor_find(Editor* config);

#endif // EDITOR_H
