#include "../src/neo.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

static void test_slice_basic(void)
{
    Slice s1 = Slice_From("hello world");
    assert(s1.size == 11 && "Slice_From should calculate length correctly");
    
    Slice s2 = Slice_Make("test", 4);
    assert(s2.size == 4);
    
    assert(!Slice_Equals(s1, s2));
    
    Slice s3 = Slice_From("test");
    assert(Slice_Equals(s2, s3));
    
    Slice sub = Slice_Subslice(s1, 0, 5);
    Slice hello = Slice_From("hello");
    assert(Slice_Equals(sub, hello));
}
