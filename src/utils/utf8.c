#include "utf8.h"

size_t Utf8SequenceLength(unsigned char leadByte)
{
    if ((leadByte & 0x80) == 0x00)
        return 1; // 0xxxxxxx: ASCII
    if ((leadByte & 0xE0) == 0xC0)
        return 2; // 110xxxxx
    if ((leadByte & 0xF0) == 0xE0)
        return 3; // 1110xxxx
    if ((leadByte & 0xF8) == 0xF0)
        return 4; // 11110xxx
    return 1; // continuation byte or invalid lead: treat as a single byte
}

bool Utf8IsContinuation(unsigned char byte) { return (byte & 0xC0) == 0x80; }

size_t Utf8PreviousBoundary(const char* text, size_t length, size_t index)
{
    if (index == 0)
        return 0;
    if (index > length)
        index = length;

    size_t candidate = index - 1;
    size_t stepsBack = 0;
    while (candidate > 0 && stepsBack < 3 && Utf8IsContinuation((unsigned char)text[candidate])) {
        candidate--;
        stepsBack++;
    }
    // Accept the found lead byte only if its declared length actually spans
    // past `index - 1`; otherwise the input is malformed -- fall back to a
    // single-byte step.
    unsigned char leadByte = (unsigned char)text[candidate];
    if (!Utf8IsContinuation(leadByte) && candidate + Utf8SequenceLength(leadByte) >= index)
        return candidate;
    return index - 1;
}

size_t Utf8NextBoundary(const char* text, size_t length, size_t index)
{
    if (index >= length)
        return length;

    size_t next = index + Utf8SequenceLength((unsigned char)text[index]);
    if (next > length)
        return length;
    // Malformed sequence (fewer continuation bytes than declared): step to
    // the first non-continuation byte instead of past it.
    for (size_t i = index + 1; i < next; i++) {
        if (!Utf8IsContinuation((unsigned char)text[i]))
            return i;
    }
    return next;
}

size_t Utf8TruncateAtBoundary(const char* text, size_t length, size_t maxBytes)
{
    if (maxBytes >= length)
        return length;

    size_t cut = maxBytes;
    while (cut > 0 && Utf8IsContinuation((unsigned char)text[cut]))
        cut--;
    return cut;
}
