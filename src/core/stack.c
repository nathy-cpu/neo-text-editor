#include "../neo.h"
#include <stdlib.h>

void Stack_Init(Stack* stack)
{
    if (!stack)
        return;
    stack->top = NULL;
    stack->size = 0;
    LOG_DEBUG("Stack_Init: initialized stack at %p", (void*)stack);
}

void Stack_Push(Stack* stack, void* data)
{
    if (!stack)
        return;
    StackNode* node = malloc(sizeof(StackNode));
    if (!node)
        return;
    node->data = data;
    node->next = stack->top;
    stack->top = node;
    stack->size++;
    LOG_DEBUG("Stack_Push: pushed node %p with data %p (size=%zu)", (void*)node, data, stack->size);
}

void* Stack_Pop(Stack* stack)
{
    if (!stack || !stack->top)
        return NULL;
    StackNode* top = stack->top;
    void* data = top->data;
    stack->top = top->next;
    free(top);
    stack->size--;
    LOG_DEBUG("Stack_Pop: popped data %p (size=%zu)", data, stack->size);
    return data;
}

void Stack_Free(Stack* stack, void (*freeData)(void*))
{
    if (!stack)
        return;
    LOG_DEBUG("Stack_Free: freeing stack at %p (size=%zu)", (void*)stack, stack->size);
    StackNode* current = stack->top;
    while (current) {
        StackNode* next = current->next;
        if (freeData && current->data) {
            freeData(current->data);
        }
        free(current);
        current = next;
    }
    stack->top = NULL;
    stack->size = 0;
}

void Stack_EnforceLimit(Stack* stack, size_t limit, void (*freeData)(void*))
{
    if (!stack || limit == 0)
        return;
    if (stack->size > limit) {
        LOG_DEBUG(
            "Stack_EnforceLimit: current size %zu exceeds limit %zu, discarding oldest items", stack->size, limit);
    }
    while (stack->size > limit) {
        StackNode* prev = NULL;
        StackNode* curr = stack->top;
        if (!curr)
            break; // Should not happen if size is tracked correctly

        while (curr->next) {
            prev = curr;
            curr = curr->next;
        }

        if (prev) {
            prev->next = NULL;
        } else {
            stack->top = NULL;
        }

        if (freeData && curr->data) {
            freeData(curr->data);
        }
        free(curr);
        stack->size--;
    }
}
