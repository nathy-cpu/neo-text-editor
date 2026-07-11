#include "../neo.h"
#include <stdlib.h>

ActionGroup* ActionGroup_New(void)
{
    ActionGroup* group = malloc(sizeof(ActionGroup));
    if (group) {
        Array_InitStruct(&group->actions, Action, 4);
    }
    return group;
}

void ActionGroup_Free(ActionGroup* group)
{
    if (!group)
        return;
    for (size_t i = 0; i < Array_Size(&group->actions); i++) {
        Action* action = (Action*)Array_At(&group->actions, i);
        Action_Free(action);
    }
    Array_Free(&group->actions);
    free(group);
}
