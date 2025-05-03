#include "slice.h"
#include <string.h>

bool Slice_Equals(Slice a, Slice b) {
    return a.size == b.size && memcmp(a.data, b.data, a.size) == 0;
}
