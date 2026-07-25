#pragma once

typedef struct Editor Editor; // Forward declaration

/**
 * @brief Moves the cursor within the current active tab buffer based on arrow key input.
 */
void Editor_MoveCursor(Editor* editor, int key);

/**
 * @brief Moves the cursor word-by-word.
 */
void Editor_MoveCursorWord(Editor* editor, int key);

/**
 * @brief Deletes the currently selected text.
 */
void Editor_DeleteSelection(Editor* editor);

/**
 * @brief Processes input character or deletion/insertion in the active tab buffer.
 */
void Editor_ProcessInput(Editor* editor, int input);

/**
 * @brief Ensures a selection is active (starts one if none).
 */
void Editor_EnsureSelection(Editor* editor);

/**
 * @brief Deletes a word in the given direction (-1 left, +1 right).
 */
void Editor_DeleteWord(Editor* editor, int direction);

/**
 * @brief Deletes from cursor to end of current line.
 */
void Editor_KillToEndOfLine(Editor* editor);

/**
 * @brief Moves current line up (-1) or down (+1).
 */
void Editor_MoveLine(Editor* editor, int direction);

/**
 * @brief Inserts an empty line below the current line.
 */
void Editor_InsertLineBelow(Editor* editor);

/**
 * @brief Inserts an empty line above the current line.
 */
void Editor_InsertLineAbove(Editor* editor);

/**
 * @brief Joins the current line with the next line.
 */
void Editor_JoinLines(Editor* editor);

/**
 * @brief Deletes the entire current line.
 */
void Editor_DeleteLine(Editor* editor);

/**
 * @brief Indents the selection (or current line) by one tab stop.
 */
void Editor_IndentLines(Editor* editor);

/**
 * @brief Un-indents the selection (or current line) by one tab stop.
 */
void Editor_UnindentLines(Editor* editor);

/**
 * @brief Processes the next keypress for the active tab or explorer.
 */
void Editor_ProcessKeypress(Editor* editor);
