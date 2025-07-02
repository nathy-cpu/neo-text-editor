#pragma once
#include "gap_buffer.h"

typedef struct {
    // Text buffer
    GapBuffer text;

    // Cursor state
    size_t cursorX;
    size_t cursorY;
    size_t renderX;

    // Text row and column offsets
    size_t rowOffset;
    size_t columnOffset;

    // File state
    char* filename;
    bool isSaved;

} Tab;

void Tab_Init(Tab* tab);

void Tab_Free(Tab* tab);

void Tab_LoadFile(Tab* tab, const char* path);

void Tab_SaveFile(Tab* tab);
