#include "editor.h"
#include "syntax.h"


EditorConfiguration editor;

void Kill()
{
    EditorKill(&editor);
}

void handleScreenResize()
{
    if (EditorGetWindowSize(&editor) == -1)
        die("EditorGetWindowSize");

    editor.screenRows -= 2;
    EditorRefreshScreen(&editor);
}

int main(int argc, char** argv)
{
    atexit(Kill);

    EditorInit(&editor);
    signal(SIGWINCH, handleScreenResize);

    if (argc >= 2)
        EditorOpenFile(&editor, argv[1], HLDB);
    else
        EditorOpenFile(&editor, "text.c", HLDB); // just for testing

    EditorSetStatusMessage(&editor, "HELP: Ctrl-Q = quit");

    while (1)
    {
        EditorRefreshScreen(&editor);
        EditorProcessKeypress(&editor, HLDB);
    }

    return 0;
}
