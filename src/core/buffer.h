#pragma once

#include "../utils/array.h"
#include "../utils/file_io.h"
#include "../utils/gap_buffer.h"
#include "../utils/slice.h"
#include "history.h"
#include "line.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum { PIECE_SOURCE_ORIGINAL, PIECE_SOURCE_ADD } PieceSource;

typedef struct {
    PieceSource source;
    // Which original mapping this piece reads from (PIECE_SOURCE_ORIGINAL
    // only): index into the buffer's retired-mappings list, or the current
    // mapping when equal to the buffer's originalGeneration. Fills the
    // padding slot next to `source`.
    uint32_t generation;
    size_t length;
    size_t start;
} Piece;

// Buffer - Main text buffer containing Piece Table and Line Cache
typedef struct Buffer {
    MappedFile mappedFile; // Mmap handle for the CURRENT original mapping
    Slice bufferOriginal; // View of the current mapping
    // Original mappings retired by saves (Array of MappedFile; index ==
    // generation). Undo/redo snapshots keep Pieces referencing them, so they
    // stay mapped (fds closed) until the buffer is freed.
    Array retiredOriginals;
    size_t originalGeneration; // == Array_Size(&retiredOriginals); generation of the current mapping
    Array bufferAdd; // Append-only dynamic add buffer; NEVER cleared while the buffer lives
    GapBuffer pieces; // GapBuffer of Pieces
    LineCache lineCache; // Line Cache gap buffer
    size_t totalBytes; // Total bytes/chars in the document
    char* filename; // File path
    bool isModified; // Modified flag
    bool isReadOnly; // Read-only flag
    size_t refCount; // Reference count
    History history; // History stack
    size_t foldedLineCount; // Number of folded lines in the document
    size_t editVersion; // Incremented on every content-mutating edit, for cache invalidation
    size_t syntaxDirtyLineStart; // Minimum line index of any edits since last syntax highlighting run

    // Line range touched by the most recent Buffer_RebuildLineCache call, so
    // other incremental consumers (e.g. visual row wrapping) can splice just
    // the affected range instead of rebuilding from scratch. Persists across
    // rebuilds so multiple consumers can each read it once per edit cycle.
    size_t lastRebuiltOldStart; // First old line index invalidated
    size_t lastRebuiltOldEnd; // Exclusive; old lines beyond this were reused as-is (shifted)
    size_t lastRebuiltNewEnd; // Exclusive; new lines beyond this are the reused/shifted tail
    bool lastRebuildOccurred; // Whether a rebuild has ever populated the fields above
    size_t rebuildSeq; // Incremented on every Buffer_RebuildLineCache; lets consumers
                       // detect that MORE THAN ONE rebuild happened since they last
                       // consumed lastRebuilt* (which only records the latest one)
} Buffer;

/**
 * @brief Creates a new empty text Buffer.
 */
Buffer* Buffer_New(void);

/**
 * @brief Frees a Buffer and all of its Lines.
 */
void Buffer_Free(Buffer* buffer);

/**
 * @brief Inserts a new Line at the specified line number index.
 */
Line* Buffer_InsertLine(Buffer* buffer, size_t lineNumber);

/**
 * @brief Deletes the line at the specified index.
 */
void Buffer_DeleteLine(Buffer* buffer, size_t lineNumber);

/**
 * @brief Gets the Line at the specified index.
 */
Line* Buffer_GetLine(const Buffer* buffer, size_t lineNumber);

/**
 * @brief Inserts a character into the buffer at the exact line and column.
 */
void Buffer_InsertChar(Buffer* buffer, size_t lineNumber, size_t column, char character);

/**
 * @brief Deletes a character from the buffer at the exact line and column.
 */
void Buffer_DeleteChar(Buffer* buffer, size_t lineNumber, size_t column);

/**
 * @brief Splits a line into two at the specified column.
 */
void Buffer_SplitLine(Buffer* buffer, size_t lineNumber, size_t column);

/**
 * @brief Joins the line at lineNumber with the line directly below it.
 */
void Buffer_JoinLine(Buffer* buffer, size_t lineNumber);

/**
 * @brief Retrieves the total number of lines in the buffer.
 */
size_t Buffer_GetLineCount(const Buffer* buffer);

/**
 * @brief Returns the first line index marked dirty by a pending edit.
 */
size_t Buffer_PeekDirtyLineStart(const Buffer* buffer);

/**
 * @brief Returns the line range touched by the most recent line cache rebuild.
 */
bool Buffer_GetLastRebuildRange(const Buffer* buffer, size_t* oldStart, size_t* oldEnd, size_t* newEnd);

/**
 * @brief Computes the total exact byte size of the buffer.
 */
size_t Buffer_GetTotalBytes(const Buffer* buffer);

/**
 * @brief Flattens the entire buffer into a single dynamically-allocated Slice.
 */
Slice Buffer_ToSlice(const Buffer* buffer);

/**
 * @brief Writes the buffer's content to `path` by streaming each piece.
 */
bool Buffer_WriteToFileStreaming(const Buffer* buffer, const char* path, bool useFsync);

/**
 * @brief Undoes the last action or action group.
 */
bool Buffer_Undo(Buffer* buffer, size_t* outLineNumber, size_t* outColumn);

/**
 * @brief Redoes the previously undone action or action group.
 */
bool Buffer_Redo(Buffer* buffer, size_t* outLineNumber, size_t* outColumn);

/**
 * @brief Raw insertion of text at a byte offset.
 */
void Buffer_InsertText(Buffer* buffer, size_t pos, const char* text, size_t len);

/**
 * @brief Raw deletion of text from start to end byte offset.
 */
void Buffer_DeleteRange(Buffer* buffer, size_t start, size_t end);

/**
 * @brief Records a history entry for a compound edit performed with raw buffer operations.
 */
void Buffer_RecordCompositeEdit(Buffer* buffer, size_t lineNumber, size_t column);

/**
 * @brief Creates a Buffer from a memory-mapped file.
 */
Buffer* Buffer_NewFromMmap(MappedFile mappedFile, const char* filename);

/**
 * @brief Invalidates the line cache starting at the given byte offset.
 */
void Buffer_InvalidateLineCache(Buffer* buffer, size_t offset, size_t newEndOffset, size_t oldTotalBytes);

/**
 * @brief Helper to ensure that folds are updated when a line is edited.
 */
void Buffer_EnsureLineVisible(Buffer* buffer, size_t lineIndex, size_t tabSize);

/**
 * @brief Callback for when a buffer is successfully saved to disk.
 */
bool Buffer_OnSave(Buffer* buffer, const char* path, bool useFsync);

/**
 * @brief Resolves the backing store a Piece reads from, honoring the piece's
 * generation (current mapping, a retired mapping, or the add buffer).
 */
const char* Buffer_PieceData(const Buffer* buffer, const Piece* piece);

/**
 * @brief Adopts a freshly-saved file's mapping as the new original source,
 * retiring the previous mapping so undo/redo snapshots stay valid. On mmap
 * failure (fileDescriptor == -1) all existing state is kept: content keeps
 * being served from the old sources and only isModified is cleared.
 * Exported as a seam so re-mmap failure is testable.
 */
void Buffer_AdoptSavedFile(Buffer* buffer, MappedFile newMmap);
