#include "../neo.h"
#include <stdlib.h>

void Stack_Init(Stack* stack)
{
    if (!stack)
        return;
    Array_Init(stack, sizeof(void*), 16, alignof(void*));
    LOG_DEBUG("Stack_Init: initialized stack at %p", (void*)stack);
}

void Stack_Push(Stack* stack, void* data)
{
    if (!stack)
        return;
    if (!stack->data) {
        Stack_Init(stack);
    }
    Array_Append(stack, &data, 1);
    LOG_DEBUG("Stack_Push: pushed data %p (size=%u)", data, stack->size);
}

void* Stack_Pop(Stack* stack)
{
    if (!stack || stack->size == 0)
        return NULL;
    void* data = Array_Get(stack, void*, stack->size - 1);
    Array_Pop(stack);
    LOG_DEBUG("Stack_Pop: popped data %p (size=%u)", data, stack->size);
    return data;
}

void Stack_Free(Stack* stack, void (*freeData)(void*))
{
    if (!stack)
        return;
    LOG_DEBUG("Stack_Free: freeing stack at %p (size=%u)", (void*)stack, stack->size);
    if (freeData) {
        for (uint32_t i = 0; i < stack->size; i++) {
            void* data = Array_Get(stack, void*, i);
            if (data) {
                freeData(data);
            }
        }
    }
    Array_Free(stack);
}

void Stack_EnforceLimit(Stack* stack, size_t limit, void (*freeData)(void*))
{
    if (!stack || limit == 0)
        return;
    if (stack->size > limit) {
        LOG_DEBUG("Stack_EnforceLimit: current size %u exceeds limit %zu, discarding oldest items", stack->size, limit);
    }
    while (stack->size > limit) {
        void* data = Array_Get(stack, void*, 0);
        if (freeData && data) {
            freeData(data);
        }
        Array_ReplaceRange(stack, 0, 1, NULL, 0);
    }
}
