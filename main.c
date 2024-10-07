#include "editor.h"
#include "syntax.h"


Editor editor;

void Kill()
{
    Editor_kill(&editor);
}

void handleScreenResize()
{
    if (Editor_getWindowSize(&editor) == -1)
        die("EditorGetWindowSize");

    editor.screenRows -= 2;
    Editor_refreshScreen(&editor);
}

int main(int argc, char** argv)
{
    atexit(Kill);

    Editor_init(&editor);
    signal(SIGWINCH, handleScreenResize);

    if (argc >= 2)
        Editor_openFile(&editor, argv[1], HLDB);
    else
        Editor_openFile(&editor, "text.c", HLDB); // just for testing

    Editor_setStatusMessage(&editor, "HELP: Ctrl-Q = quit");

    while (1)
    {
        Editor_refreshScreen(&editor);
        Editor_processKeypress(&editor, HLDB);
    }

    return 0;
}
