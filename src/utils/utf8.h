#pragma once

#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>

// Byte-level UTF-8 helpers. All functions are pure and tolerant of invalid
// or truncated sequences: on malformed input they degrade to single-byte
// steps so callers can never get stuck or walk out of bounds.

/** Length in bytes of the sequence starting with `leadByte` (1..4; 1 for invalid leads). */
size_t Utf8SequenceLength(unsigned char leadByte);

/** True for UTF-8 continuation bytes (10xxxxxx). */
bool Utf8IsContinuation(unsigned char byte);

/**
 * @brief Start index of the codepoint strictly before `index`.
 * Backs over at most 3 continuation bytes; falls back to index-1 on
 * malformed input. `index` must be > 0.
 */
size_t Utf8PreviousBoundary(const char* text, size_t length, size_t index);

/**
 * @brief Start index of the codepoint strictly after the one at `index`.
 * Clamped to `length`. Falls back to index+1 on malformed input.
 */
size_t Utf8NextBoundary(const char* text, size_t length, size_t index);

/**
 * @brief Largest cut point <= maxBytes that does not split a UTF-8 sequence.
 */
size_t Utf8TruncateAtBoundary(const char* text, size_t length, size_t maxBytes);

// ctype wrappers: the <ctype.h> functions have undefined behavior for plain
// char arguments whose value is negative (any byte >= 0x80 on signed-char
// platforms, i.e. all UTF-8 continuation/lead bytes). Always classify through
// these.

static inline int ByteIsSpace(char c) { return isspace((unsigned char)c); }
static inline int ByteIsDigit(char c) { return isdigit((unsigned char)c); }
static inline int ByteIsControl(char c) { return iscntrl((unsigned char)c); }
