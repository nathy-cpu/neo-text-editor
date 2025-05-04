#include "slice.h"
#include <string.h>

bool Slice_Equals(Slice a, Slice b)
{
    return a.size == b.size && memcmp(a.data, b.data, a.size) == 0;
}

Slice Slice_Make(const void* data, size_t size)
{
    return (Slice) { .data = data, .size = size };
}

Slice Slice_From(const void* str)
{
    return (Slice) { .data = str, .size = strlen(str) };
}
