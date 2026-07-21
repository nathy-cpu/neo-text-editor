#pragma once

#include "slice.h"

/**
 * @brief Writes text to the system clipboard (if available) and internal clipboard.
 * @param text The null-terminated string to write.
 */
void Clipboard_Write(const char* text);

/**
 * @brief Begins an incremental write to the clipboard.
 */
void Clipboard_BeginWrite(void);

/**
 * @brief Appends text to the clipboard during an incremental write.
 */
void Clipboard_Append(const char* data, size_t size);

/**
 * @brief Ends an incremental write, finalizing the clipboard contents and writing to system clipboard.
 */
void Clipboard_EndWrite(void);

/**
 * @brief Reads text from the system clipboard (if available) or internal clipboard.
 * @return A Slice representing the clipboard text. The data is owned by the clipboard module
 *         and must not be freed by the caller.
 */
Slice Clipboard_Read(void);

/**
 * @brief Frees resources allocated by the clipboard system on editor exit.
 */
void Clipboard_Free(void);
