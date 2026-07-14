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

static void test_slice_make_and_zero_size(void)
{
    Slice s = Slice_Make(NULL, 0);
    assert(s.data == NULL && s.size == 0);

    Slice empty1 = Slice_Make("", 0);
    Slice empty2 = Slice_Make("anything", 0);
    assert(Slice_Equals(empty1, empty2) && "Two zero-size slices are equal regardless of their data pointers");

    Slice nonEmpty = Slice_From("x");
    assert(!Slice_Equals(empty1, nonEmpty));
    assert(!Slice_Equals(nonEmpty, empty1));
}

static void test_slice_subslice_bounds(void)
{
    Slice s = Slice_From("hello");

    // Empty subslice at the very end (start == end == size) must be valid, not rejected.
    Slice tailEmpty = Slice_Subslice(s, 5, 5);
    assert(tailEmpty.size == 0);

    // Full-length subslice.
    Slice full = Slice_Subslice(s, 0, 5);
    assert(Slice_Equals(full, s));

    // Empty subslice in the middle.
    Slice midEmpty = Slice_Subslice(s, 2, 2);
    assert(midEmpty.size == 0);

    // Out-of-bounds end.
    Slice oobEnd = Slice_Subslice(s, 0, 6);
    assert(oobEnd.data == NULL && oobEnd.size == 0);

    // start > end must be rejected outright, not underflow into a huge size.
    Slice inverted = Slice_Subslice(s, 3, 1);
    assert(inverted.data == NULL && inverted.size == 0);

    // Zero-size slice: only start == end == 0 is valid.
    Slice zero = Slice_Make(NULL, 0);
    Slice zeroSub = Slice_Subslice(zero, 0, 0);
    assert(zeroSub.size == 0);
    Slice zeroOob = Slice_Subslice(zero, 1, 1);
    assert(zeroOob.data == NULL && zeroOob.size == 0);
}
