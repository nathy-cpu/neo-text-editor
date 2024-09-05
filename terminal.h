#ifndef TERMINAL_H
#define TERMINAL_H

#include "dependencies.h"

#define CTRL_KEY(k) ((k) & 0x1f)

enum Key
{
    BACKSPACE = 127,
    ARROW_LEFT = 2000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    DELETE_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN
};

/******* initializing the terminal ********/

void         die(const char* source);

short int    readKeypress();


#endif // TERMINAL_H
