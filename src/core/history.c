#include "../neo.h"
#include <stdlib.h>

void History_Init(History* history)
{
    if (!history)
        return;
    Stack_Init(&history->undoStack);
    Stack_Init(&history->redoStack);
    history->currentGroup = NULL;
    history->isUndoRedoing = false;
    history->undoLimit = 1000;
}

void History_Free(History* history)
{
    if (!history)
        return;
    Stack_Free(&history->undoStack, (void (*)(void*))ActionGroup_Free);
    Stack_Free(&history->redoStack, (void (*)(void*))ActionGroup_Free);
}
