#include "../src/core/history.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

TEST(history, stack_basic)
{
    Stack stack;
    Stack_Init(&stack);
    assert(stack.size == 0);

    int a = 1, b = 2, c = 3;
    Stack_Push(&stack, &a);
    Stack_Push(&stack, &b);
    Stack_Push(&stack, &c);
    assert(stack.size == 3);

    assert(Stack_Pop(&stack) == &c);
    assert(Stack_Pop(&stack) == &b);
    assert(Stack_Pop(&stack) == &a);
    assert(stack.size == 0);

    assert(Stack_Pop(&stack) == NULL);
    assert(stack.size == 0);
    Stack_Free(&stack, NULL);
}

static int freedCount = 0;
static void CountingFree(void* data)
{
    (void)data;
    freedCount++;
}

TEST(history, stack_enforce_limit)
{
    Stack stack;
    Stack_Init(&stack);

    int values[5] = { 10, 20, 30, 40, 50 };
    for (int i = 0; i < 5; i++) {
        Stack_Push(&stack, &values[i]);
    }
    assert(stack.size == 5);

    freedCount = 0;
    Stack_EnforceLimit(&stack, 2, CountingFree);
    assert(stack.size == 2);
    assert(freedCount == 3);

    // The two most recently pushed values must survive (oldest are evicted).
    assert(Stack_Pop(&stack) == &values[4]);
    assert(Stack_Pop(&stack) == &values[3]);
    assert(Stack_Pop(&stack) == NULL);
    Stack_Free(&stack, NULL);
}

static void CountingFreeAndRelease(void* data)
{
    freedCount++;
    free(data);
}

TEST(history, stack_free)
{
    Stack stack;
    Stack_Init(&stack);

    for (int i = 0; i < 3; i++) {
        int* value = malloc(sizeof(int));
        *value = i;
        Stack_Push(&stack, value);
    }
    assert(stack.size == 3);

    freedCount = 0;
    Stack_Free(&stack, CountingFreeAndRelease);
    assert(freedCount == 3);
    assert(stack.size == 0);
}

TEST(history, action_group_lifecycle)
{
    ActionGroup* group = ActionGroup_New();
    assert(group != NULL);
    assert(Array_Size(&group->actions) == 0);
    assert(group->snapshotBefore == NULL);
    assert(group->snapshotAfter == NULL);

    Action charAction = { .type = ACTION_INSERT_CHAR, .lineNumber = 0, .column = 0, .payload = { .character = 'x' } };
    Array_Append(&group->actions, &charAction, 1);

    char* text = malloc(3);
    memcpy(text, "abc", 3);
    Action textAction
        = { .type = ACTION_INSERT_TEXT, .lineNumber = 0, .column = 1, .payload = { .text = { .data = text, .size = 3 } } };
    Array_Append(&group->actions, &textAction, 1);

    assert(Array_Size(&group->actions) == 2);

    // ASan verifies Action_Free frees payload.text.data for the ACTION_INSERT_TEXT
    // entry and ActionGroup_Free doesn't leak the group/actions array.
    ActionGroup_Free(group);
}

TEST(history, history_init_free)
{
    History history;
    History_Init(&history);
    assert(history.undoStack.size == 0);
    assert(history.redoStack.size == 0);
    assert(history.currentGroup == NULL);
    assert(history.isUndoRedoing == false);
    assert(history.undoLimit == 1000);

    ActionGroup* undoGroup = ActionGroup_New();
    ActionGroup* redoGroup = ActionGroup_New();
    Stack_Push(&history.undoStack, undoGroup);
    Stack_Push(&history.redoStack, redoGroup);
    assert(history.undoStack.size == 1);
    assert(history.redoStack.size == 1);

    // ASan verifies both pushed groups are freed via the ActionGroup_Free callback.
    History_Free(&history);
}
