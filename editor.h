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

} EditorConfiguration;

/******* initializing the Editor ********/

void    disableRawMode(EditorConfiguration *config);

void    enableRawMode(EditorConfiguration *config);

void    EditorKill(EditorConfiguration *config);

int     EditorGetCursorPosition(EditorConfiguration *config);

int     EditorGetWindowSize(EditorConfiguration *config);

void    EditorInit(EditorConfiguration *config);

void    EditorSetSyntaxHighlight(EditorConfiguration *config, Syntax HLDB[]);

/******* handling files to edit ********/

void    EditorOpenFile(EditorConfiguration *config, const char* filename, Syntax HLDB[]);

void    EditorSaveToFile(EditorConfiguration *config, Syntax HLDB[]);

/******* Editor output ********/

void    EditorScroll(EditorConfiguration *config);

void    EditorSetStatusMessage(EditorConfiguration *config, const char* fstring, ...);

void    EditorDrawRows(EditorConfiguration *config, ScreenBuffer* sbuf);

void    EditorDrawStatusBar(EditorConfiguration *config, ScreenBuffer* sbuf);

void    EditorDrawMessageBar(EditorConfiguration *config, ScreenBuffer* sbuf);

void    EditorRefreshScreen(EditorConfiguration *config);

/******* input ********/

char*   EditorPromptForInput(EditorConfiguration *config, char* prompt, void (*callBackFunction)(EditorConfiguration*, char*, int));

void    EditorMoveCursor(EditorConfiguration *config, short int key);

void    EditorProcessKeypress(EditorConfiguration *config, Syntax HLDB[]);


/******* Editor operations ********/

void    EditorInsertChar(EditorConfiguration *config, short int input);

void    EditorInsertNewLine(EditorConfiguration *config);

void    EditorDeleteChar(EditorConfiguration *config);

/******* text search ********/

void findCallBack(EditorConfiguration* config, char* query, int key);

void EditorFind(EditorConfiguration* config);

#endif // EDITOR_H
