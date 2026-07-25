#pragma once

#include "../utils/array.h"
#include "../utils/gap_buffer.h"
#include "../utils/slice.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    ACTION_INSERT_CHAR,
    ACTION_DELETE_CHAR,
    ACTION_INSERT_TEXT,
    ACTION_DELETE_TEXT,
    ACTION_COMPOSITE_EDIT,
    ACTION_SPLIT_LINE,
    ACTION_JOIN_LINE
} ActionType;

typedef struct {
    ActionType type;
    uint32_t lineNumber;
    uint32_t column;

    union {
        char character;
        Slice text;
    } payload;
} Action;

typedef struct {
    GapBuffer pieces;
    size_t totalBytes;
} DocumentSnapshot;

typedef struct ActionGroup {
    Array actions;
    DocumentSnapshot* snapshotBefore;
    DocumentSnapshot* snapshotAfter;
} ActionGroup;

typedef Array Stack;

struct Buffer; // Forward declaration of Buffer to break circular dependency

void Stack_Init(Stack* stack);
void Stack_Push(Stack* stack, void* data);
void* Stack_Pop(Stack* stack);
void Stack_Free(Stack* stack, void (*freeData)(void*));
void Stack_EnforceLimit(Stack* stack, size_t limit, void (*freeData)(void*));

typedef struct {
    Stack undoStack;
    Stack redoStack;
    ActionGroup* currentGroup;
    bool isUndoRedoing;
    bool forceGrouping;
    size_t undoLimit;
} History;

void History_Init(History* history);
void History_Free(History* history);

void Action_Free(Action* action);
ActionGroup* ActionGroup_New(void);
void ActionGroup_Free(ActionGroup* group);

DocumentSnapshot* DocumentSnapshot_Copy(struct Buffer* buffer);
void DocumentSnapshot_Free(DocumentSnapshot* snap);
void Buffer_RestoreSnapshot(struct Buffer* buffer, DocumentSnapshot* snap, size_t rangeStartOffset);
