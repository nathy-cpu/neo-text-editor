#include "../src/utils/utf8.h"
#include <assert.h>
#include <string.h>

// "a" + e-acute (2 bytes) + Ethiopic HA (3 bytes) + emoji (4 bytes) + "z"
static const char* mixedText = "a\xC3\xA9\xE1\x88\x80\xF0\x9F\x98\x80z";
// byte offsets:  a=0, é=1..2, ሀ=3..5, 😀=6..9, z=10; length 11

TEST(utf8, sequence_length)
{
    assert(Utf8SequenceLength('a') == 1);
    assert(Utf8SequenceLength(0xC3) == 2);
    assert(Utf8SequenceLength(0xE1) == 3);
    assert(Utf8SequenceLength(0xF0) == 4);
    // Continuation and invalid leads degrade to 1
    assert(Utf8SequenceLength(0x80) == 1);
    assert(Utf8SequenceLength(0xFF) == 1);
}

TEST(utf8, is_continuation)
{
    assert(!Utf8IsContinuation('a'));
    assert(Utf8IsContinuation(0x80));
    assert(Utf8IsContinuation(0xBF));
    assert(!Utf8IsContinuation(0xC3));
}

TEST(utf8, next_boundary_walks_codepoints)
{
    size_t length = strlen(mixedText);
    assert(Utf8NextBoundary(mixedText, length, 0) == 1);
    assert(Utf8NextBoundary(mixedText, length, 1) == 3);
    assert(Utf8NextBoundary(mixedText, length, 3) == 6);
    assert(Utf8NextBoundary(mixedText, length, 6) == 10);
    assert(Utf8NextBoundary(mixedText, length, 10) == 11);
    assert(Utf8NextBoundary(mixedText, length, 11) == 11); // clamped at end
}

TEST(utf8, previous_boundary_walks_codepoints)
{
    size_t length = strlen(mixedText);
    assert(Utf8PreviousBoundary(mixedText, length, 11) == 10);
    assert(Utf8PreviousBoundary(mixedText, length, 10) == 6);
    assert(Utf8PreviousBoundary(mixedText, length, 6) == 3);
    assert(Utf8PreviousBoundary(mixedText, length, 3) == 1);
    assert(Utf8PreviousBoundary(mixedText, length, 1) == 0);
    assert(Utf8PreviousBoundary(mixedText, length, 0) == 0);
}

TEST(utf8, boundaries_tolerate_malformed_input)
{
    // Truncated 3-byte sequence followed by ASCII
    const char* truncated = "\xE1\x88x";
    assert(Utf8NextBoundary(truncated, 3, 0) == 2); // stops at first non-continuation
    // Lone continuation bytes step one at a time
    const char* loneContinuations = "\x80\x80\x80";
    assert(Utf8NextBoundary(loneContinuations, 3, 0) == 1);
    assert(Utf8PreviousBoundary(loneContinuations, 3, 3) == 2);
    // Lead byte that declares less than the walked-back distance falls back to -1
    const char* overlongTail = "a\x80\x80\x80";
    assert(Utf8PreviousBoundary(overlongTail, 4, 4) == 3);
}

TEST(utf8, truncate_at_boundary_every_offset)
{
    size_t length = strlen(mixedText);
    // Codepoint boundaries are at byte offsets 0, 1, 3, 6, 10, 11.
    size_t expected[] = { 0, 1, 1, 3, 3, 3, 6, 6, 6, 6, 10 };
    for (size_t maxBytes = 0; maxBytes < length; maxBytes++) {
        size_t cut = Utf8TruncateAtBoundary(mixedText, length, maxBytes);
        assert(cut == expected[maxBytes]);
    }
    assert(Utf8TruncateAtBoundary(mixedText, length, length) == length);
    assert(Utf8TruncateAtBoundary(mixedText, length, length + 5) == length);
}

TEST(utf8, byte_classifiers_accept_high_bytes)
{
    // UB-free classification of bytes >= 0x80 (would be negative as plain char)
    assert(!ByteIsSpace('\xC3'));
    assert(!ByteIsDigit('\xA9'));
    assert(!ByteIsControl('\xE1'));
    assert(ByteIsSpace(' '));
    assert(ByteIsDigit('7'));
    assert(ByteIsControl('\x01'));
}
