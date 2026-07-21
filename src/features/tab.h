#pragma once

#include "../core/buffer.h"
#include "../utils/array.h"
#include "config.h"
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    size_t lineIndex; // 0-based logical line index
    size_t startCol; // byte index in logical line
    size_t length; // number of bytes in this segment
    bool isWrapped; // true if this is a wrapped segment
} VisualRow;

typedef struct {
    // Text buffer
    Buffer* buffer;

    // Syntax Highlighting
    Syntax* syntax;
    // Exclusive bound: lines [0, syntaxHighWaterMark) have been highlighted at
    // least once. Lets Tab_UpdateSyntax extend coverage lazily as the
    // viewport scrolls, instead of highlighting the whole file at load.
    size_t syntaxHighWaterMark;

    // Cursor state
    size_t cursorX;
    size_t cursorY;
    size_t renderX;

    // Text row and column offsets
    size_t rowOffset;
    size_t columnOffset;

    // File state
    char* filename;
    bool isSaved;

    // Selection state
    bool hasSelection;
    size_t selectStartX;
    size_t selectStartY;

    // Visual wrapping state
    Array visualRows; // Array of VisualRow
    size_t visualRowsEditVersion; // Buffer editVersion visualRows was last built for
    size_t visualRowsFoldedCount; // Buffer foldedLineCount visualRows was last built for
    size_t visualRowsUsableColumns; // usableColumns visualRows was last built for
    size_t visualRowsLineCount; // Buffer line count visualRows was last built for

    // True if this tab's line count exceeded config->wrapDisableLineThreshold
    // at load time, overriding config->wrapLines to false for this tab only.
    bool wrapLinesDisabledForSize;

    // Pointer to active configuration
    Config* config;
} Tab;

struct Editor; // Forward declaration

/**
 * @brief Initializes a new Tab.
 */
void Tab_Init(Tab* tab);

/**
 * @brief Frees the resources associated with a Tab.
 */
void Tab_Free(Tab* tab);

/**
 * @brief Loads a file from disk into the Tab's buffer.
 */
void Tab_LoadFile(Tab* tab, const char* path);

/**
 * @brief Saves the Tab's buffer back to disk.
 */
void Tab_SaveFile(Tab* tab);

/**
 * @brief Whether this tab should currently word-wrap its lines.
 */
bool Tab_ShouldWrapLines(const Tab* tab);

/**
 * @brief Triggers a syntax highlight update for the entire tab.
 */
void Tab_SetSyntaxHighlight(Tab* tab);

/**
 * @brief Re-evaluates syntax highlighting, extending coverage as needed.
 */
void Tab_UpdateSyntax(Tab* tab, size_t maxLine);

/**
 * @brief Computes the number of digits required for line numbers.
 */
size_t Tab_GetGutterDigits(const Tab* tab);

/**
 * @brief Computes the total visual width of the line number gutter.
 */
size_t Tab_GetGutterWidth(const Tab* tab);

/**
 * @brief Retrieves the visual row index of the cursor.
 */
size_t Tab_GetCursorVRowIdx(const Tab* tab);

/**
 * @brief Retrieves the total number of visual rows.
 */
size_t Tab_GetVisualRowCount(const Tab* tab);

/**
 * @brief Retrieves the visual column offset of the cursor in a specific visual row.
 */
size_t Tab_GetCursorVisualCol(const Tab* tab, size_t vrowIdx);

/**
 * @brief Sets the logical cursor position based on visual row and target visual column.
 */
void Tab_SetCursorFromVRow(Tab* tab, size_t targetVRowIdx, size_t targetVisualCol);

/**
 * @brief Re-evaluates visual wrapping segments for the entire buffer.
 */
void Tab_UpdateVisualRows(const struct Editor* editor, Tab* tab, size_t usableColumns);

/**
 * @brief Gets the exact start and end coordinates of the active text selection.
 */
void Tab_GetSelection(Tab* tab, size_t* startX, size_t* startY, size_t* endX, size_t* endY);

/**
 * @brief Retrieves the selected text as a dynamically allocated null-terminated string.
 */
char* Tab_GetSelectedText(Tab* tab);

/**
 * @brief Copies the active selection to the clipboard using the streaming/incremental API (zero-allocation).
 */
void Tab_CopySelection(Tab* tab);

/**
 * @brief Checks if a logical line index is currently visible on screen.
 */
bool Tab_IsLineVisible(const Tab* tab, size_t lineIndex);

/**
 * @brief Finds the next visible line index starting from the given line.
 */
size_t Tab_NextVisibleLine(const Tab* tab, size_t lineIndex);

/**
 * @brief Finds the previous visible line index starting from the given line.
 */
size_t Tab_PrevVisibleLine(const Tab* tab, size_t lineIndex);
