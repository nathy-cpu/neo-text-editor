#pragma once

#include "../utils/array.h"
#include "../utils/gap_buffer.h"
#include "../utils/slice.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LINE_HUGE_THRESHOLD 8192

typedef struct {
    uint32_t rawCol;
    uint32_t renderCol;
} RenderCheckpoint;

// Line - Represents a single line in the text buffer
typedef struct Line {
    size_t offset; // Absolute byte offset in the Piece Table
    size_t length; // Length of the line (excluding trailing newline)
    Array* styles; // Highlighting styles array
    Array* renderCheckpoints; // Render-column checkpoints for huge lines
    uint32_t renderCheckpointTabSize;
    uint16_t foldLevel; // Indentation fold level
    bool isFolded; // Whether line is folded
    bool commentStateOut; // Multi-line-comment state after this line
    bool commentStateOutValid; // Whether commentStateOut reflects the line's current content
} Line;

typedef struct {
    GapBuffer lines; // Gap buffer of Line structures
    size_t dirtyLineStart; // Rebuilding starting mark
    size_t dirtyOffsetEnd; // Rebuilding ending byte offset in the new buffer
    size_t oldTotalBytes; // Total bytes before the edit
} LineCache;

struct Buffer; // Forward declaration

/**
 * @brief Frees the Line object and its internal gap buffers.
 */
void Line_Free(Line* line);

/**
 * @brief Gets the logical character length of the line.
 */
size_t Line_Length(Line* line);

/**
 * @brief Returns a Slice representing the line's text.
 */
Slice Line_GetText(Line* line, struct Buffer* buffer);

/**
 * @brief Returns a Slice covering just [start, start+length) of the line's text.
 */
Slice Line_GetTextRange(Line* line, struct Buffer* buffer, size_t start, size_t length);

/**
 * @brief Converts a logical byte position to a visual render column.
 */
size_t Line_GetRenderX(Line* line, struct Buffer* buffer, size_t cursorX, size_t tabSize);

/**
 * @brief Gets a character at index in the line.
 */
char Line_GetChar(Line* line, struct Buffer* buffer, size_t index);

/**
 * @brief Calculates indentation width of a line, expanding tab stops.
 */
size_t Line_GetIndentation(Line* line, struct Buffer* buffer, size_t tabSize);

/**
 * @brief Checks if a line consists entirely of whitespace.
 */
bool Line_IsBlank(Line* line, struct Buffer* buffer);

/**
 * @brief Checks if a line is foldable based on indentation levels.
 */
bool Line_IsFoldable(const struct Buffer* buffer, size_t lineNumber, size_t tabSize);
