#pragma once

#include "../utils/array.h"

typedef struct Editor Editor; // Forward declaration

/**
 * @brief Toggles the logs overlay view.
 */
void Editor_ToggleLogs(Editor* editor);

/**
 * @brief Renders the logs overlay.
 */
void Editor_DrawLogs(Editor* editor, Array* screenBuffer);

/**
 * @brief Processes input keys while the logs view is active.
 */
void Editor_ProcessLogsInput(Editor* editor, int input);
