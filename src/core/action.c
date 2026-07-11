#include "../neo.h"
#include <stdlib.h>

void Action_Free(Action* action)
{
    if (!action)
        return;
    if (action->type == ACTION_INSERT_TEXT || action->type == ACTION_DELETE_TEXT) {
        if (action->payload.text.data) {
            free((void*)action->payload.text.data);
            action->payload.text.data = NULL;
            action->payload.text.size = 0;
        }
    }
}
