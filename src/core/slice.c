#include "../neo.h"
#include <string.h>

bool Slice_Equals(Slice a, Slice b) { return a.size == b.size && memcmp(a.data, b.data, a.size) == 0; }

Slice Slice_Make(const void* data, size_t size) { return (Slice) { .data = data, .size = size }; }

Slice Slice_From(const void* string) { return (Slice) { .data = string, .size = strlen(string) }; }

Slice Slice_Subslice(Slice slice, size_t start, size_t end)
{
    if (start > slice.size - 1 || end > slice.size)
        return (Slice) { .data = NULL, .size = 0 };

    return (Slice) { .data = (const char*)slice.data + start, .size = end - start };
}
