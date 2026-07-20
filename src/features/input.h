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
 * @brief Processes the next keypress for the active tab or explorer.
 */
void Editor_ProcessKeypress(Editor* editor);
