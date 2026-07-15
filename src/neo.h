#pragma once

// Guarded so this header stays a no-op re-define when something upstream
// (e.g. a translation unit that pulled in a system header before this one)
// already set these -- an unconditional #define here would otherwise clash
// with a differently-spelled value <features.h> computed for the same macro.
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifndef _BSD_SOURCE
#define _BSD_SOURCE
#endif
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <signal.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>

#include <lua5.4/lauxlib.h>
#include <lua5.4/lua.h>
#include <lua5.4/lualib.h>

// ============================================================================
// CORE DATA STRUCTURES
// ============================================================================

// Slice - Type-agnostic slice/string view
typedef struct {
    const void* data;
    size_t size;
} Slice;

typedef struct {
    Slice content;
    int fileDescriptor; // File descriptor for cleanup
} MappedFile;

/**
 * @brief Creates a new Slice from a raw pointer and size.
 * @param data Pointer to the start of the data.
 * @param size Number of bytes in the slice.
 * @return A constructed Slice view.
 */
Slice Slice_Make(const void* data, size_t size);

/**
 * @brief Creates a new Slice from a null-terminated C string.
 * @param string Null-terminated string to view.
 * @return A constructed Slice view.
 */
Slice Slice_From(const void* string);

/**
 * @brief Compares two Slices for exact byte-for-byte equality.
 * @param a First slice.
 * @param b Second slice.
 * @return True if they are identical in size and content, false otherwise.
 */
bool Slice_Equals(Slice a, Slice b);

/**
 * @brief Creates a sub-slice from an existing slice.
 * @param slice The original slice.
 * @param start The starting byte index.
 * @param end The ending byte index (exclusive).
 * @return A new Slice representing the specified range.
 */
Slice Slice_Subslice(Slice slice, size_t start, size_t end);

// Array - Type-agnostic arraylist
typedef struct {
    void* data;
    size_t size;
    size_t capacity;
    size_t itemSize;
    size_t alignment;
} Array;

/**
 * @brief Initializes a dynamic array.
 * @param array Pointer to the array to initialize.
 * @param itemSize Size of a single item (e.g., sizeof(int)).
 * @param capacity Initial capacity in items.
 * @param alignment Memory alignment requirement.
 */
void Array_Init(Array* array, size_t itemSize, size_t capacity, size_t alignment);

/**
 * @brief Frees the underlying memory of the array.
 * @param array Pointer to the array to free.
 */
void Array_Free(Array* array);

/**
 * @brief Clears the array by resetting its size to 0 (does not free memory).
 * @param array Pointer to the array to clear.
 */
void Array_Clear(Array* array);

/**
 * @brief Appends multiple items to the end of the array, resizing if necessary.
 * @param array Pointer to the array.
 * @param items Pointer to the items to append.
 * @param count Number of items to append.
 * @return True on success, false on allocation failure.
 */
bool Array_Append(Array* array, const void* items, size_t count);

/**
 * @brief Removes the last item from the array.
 * @param array Pointer to the array.
 * @return True if an item was popped, false if the array was empty.
 */
bool Array_Pop(Array* array);

/**
 * @brief Replaces a range of items in-place with a different number of items,
 * shifting the tail as needed (memmove-based; only safe for POD element types
 * with no owned pointers).
 * @param array Pointer to the array.
 * @param start Index of the first item to replace.
 * @param count Number of existing items to remove starting at `start`.
 * @param newItems Pointer to the replacement items (may be NULL if newCount is 0).
 * @param newCount Number of replacement items.
 * @return True on success, false on allocation failure.
 */
bool Array_ReplaceRange(Array* array, size_t start, size_t count, const void* newItems, size_t newCount);

/**
 * @brief Gets a pointer to the item at the specified index.
 * @param array Pointer to the array.
 * @param index Index of the item.
 * @return Pointer to the item, or NULL if out of bounds (based on logical size).
 */
void* Array_At(const Array* array, size_t index);

/**
 * @brief Gets a raw pointer to an item at an index up to the capacity.
 * @param array Pointer to the array.
 * @param index Index of the item.
 * @return Pointer to the item, or NULL if out of bounds (based on capacity).
 */
void* Array_RawAt(const Array* array, size_t index);

/**
 * @brief Returns the number of items currently in the array.
 * @param array Pointer to the array.
 * @return Number of items.
 */
size_t Array_Size(const Array* array);

/**
 * @brief Returns a Slice representing the entire array.
 * @param array Pointer to the array.
 * @return A Slice view of the array's used data.
 */
Slice Array_ToSlice(const Array* array);

// Type-safe macros
#define Array_InitChar(array, capacity) Array_Init(array, sizeof(char), capacity, 64) // Cache line
#define Array_InitStruct(array, type, capacity) Array_Init(array, sizeof(type), capacity, alignof(type))
#define Array_Get(array, type, index) (*(type*)Array_At(array, index))
#define Array_AppendSlice(array, slice) Array_Append(array, slice.data, slice.size)

// GapBuffer - Type-agnostic gap buffer
typedef struct {
    Array data;
    size_t gapStart;
    size_t gapEnd;
} GapBuffer;

/**
 * @brief Initializes a gap buffer with a specific item size.
 */
void GapBuffer_Init(GapBuffer* gapBuffer, size_t itemSize, size_t initialCapacity, size_t alignment);

/**
 * @brief Frees resources associated with the gap buffer.
 */
void GapBuffer_Free(GapBuffer* gapBuffer);

/**
 * @brief Gets a pointer to the item at a specific logical position in the gap buffer.
 */
void* GapBuffer_At(const GapBuffer* gapBuffer, size_t index);

/**
 * @brief Inserts a slice of data at a logical position in the gap buffer.
 */
void GapBuffer_InsertSlice(GapBuffer* gapBuffer, size_t position, Slice content);

/**
 * @brief Inserts a single character at a logical position (for char gap buffers).
 */
void GapBuffer_InsertChar(GapBuffer* gapBuffer, size_t position, char content);

/**
 * @brief Deletes a sequence of items from the gap buffer.
 */
void GapBuffer_Delete(GapBuffer* gapBuffer, size_t position, size_t size);

/**
 * @brief Flattens a char gap buffer into a dynamically allocated contiguous slice.
 */
Slice GapBuffer_ToSlice(GapBuffer* gapBuffer);

/**
 * @brief Clears the entire gap buffer.
 */
void GapBuffer_Clear(GapBuffer* gapBuffer);

/**
 * @brief Returns the total logical size (amount of elements) in the gap buffer.
 */
size_t GapBuffer_Size(const GapBuffer* gapBuffer);

/**
 * @brief Moves the gap to the specified new logical position.
 */
void GapBuffer_MoveGap(GapBuffer* gapBuffer, size_t newGapStart);

// ============================================================================
// UNDO/REDO HISTORY
// ============================================================================

typedef enum {
    ACTION_INSERT_CHAR,
    ACTION_DELETE_CHAR,
    ACTION_INSERT_TEXT,
    ACTION_DELETE_TEXT,
    ACTION_SPLIT_LINE,
    ACTION_JOIN_LINE
} ActionType;

typedef struct {
    ActionType type;
    size_t lineNumber;
    size_t column;

    union {
        char character;
        Slice text;
    } payload;
} Action;

typedef struct {
    GapBuffer pieces;
    size_t totalBytes;
} DocumentSnapshot;

typedef struct ActionGroup {
    Array actions;
    DocumentSnapshot* snapshotBefore;
    DocumentSnapshot* snapshotAfter;
} ActionGroup;

typedef struct StackNode {
    ActionGroup* data;
    struct StackNode* next;
} StackNode;

typedef struct {
    StackNode* top;
    size_t size;
} Stack;

void Stack_Init(Stack* stack);
void Stack_Push(Stack* stack, void* data);
void* Stack_Pop(Stack* stack);
void Stack_Free(Stack* stack, void (*freeData)(void*));
void Stack_EnforceLimit(Stack* stack, size_t limit, void (*freeData)(void*));

typedef struct {
    Stack undoStack;
    Stack redoStack;
    ActionGroup* currentGroup;
    bool isUndoRedoing;
    size_t undoLimit;
} History;

void History_Init(History* history);
void History_Free(History* history);

void Action_Free(Action* action);
ActionGroup* ActionGroup_New(void);
void ActionGroup_Free(ActionGroup* group);

// ============================================================================
// PIECE TABLE & LINE CACHE BUFFER
// ============================================================================

typedef enum { PIECE_SOURCE_ORIGINAL, PIECE_SOURCE_ADD } PieceSource;

typedef struct {
    PieceSource source;
    size_t start;
    size_t length;
} Piece;

struct Buffer;

// Lines longer than this are considered "huge" throughout rendering: they
// skip word-wrap (one unwrapped visual row instead of an O(length) tab-stop
// walk to find wrap points), skip syntax highlighting, use a checkpoint index
// for Line_GetRenderX instead of an O(length) walk, and are rendered via
// Line_GetTextRange (a bounded sub-range) rather than materializing/caching
// the whole line via Line_GetText.
#define LINE_HUGE_THRESHOLD 8192

// A lazily-built prefix-sum checkpoint for Line_GetRenderX on huge lines:
// renderCol is the tab-expanded render column at raw byte offset rawCol.
typedef struct {
    size_t rawCol;
    size_t renderCol;
} RenderCheckpoint;

// Line - Represents a single line in the text buffer
typedef struct Line {
    struct Buffer* buffer; // Parent buffer (NULL for standalone tests)
    size_t offset; // Absolute byte offset in the Piece Table
    size_t length; // Length of the line (excluding trailing newline)
    char* text; // Flattened/cached text view of the line (lazily loaded, or NULL)
    Array styles; // Highlighting styles array (dynamic array of char)
    size_t lineNumber; // 0-based logical line number
    bool isFolded; // Whether line is folded
    size_t foldLevel; // Indentation fold level
    Array testText; // Standalone test text buffer
    bool commentStateOut; // Multi-line-comment state after this line, for incremental syntax updates
    bool commentStateOutValid; // Whether commentStateOut reflects the line's current content
    // Render-column checkpoints for huge lines (NULL until built; only built
    // above a size threshold -- see Line_GetRenderX). Rebuilt if the tab size
    // it was built for no longer matches.
    RenderCheckpoint* renderCheckpoints;
    size_t renderCheckpointCount;
    size_t renderCheckpointTabSize;
} Line;

typedef struct {
    GapBuffer lines; // Gap buffer of Line structures
    size_t dirtyLineStart; // Rebuilding starting mark
    size_t dirtyOffsetEnd; // Rebuilding ending byte offset in the new buffer
    size_t oldTotalBytes; // Total bytes before the edit
} LineCache;

// Buffer - Main text buffer containing Piece Table and Line Cache
typedef struct Buffer {
    MappedFile mappedFile; // Mmap handle for file cleanup
    Slice bufferOriginal; // Immutable original buffer
    Array bufferAdd; // Append-only dynamic add buffer
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

    // Line range touched by the most recent Buffer_RebuildLineCache call, so
    // other incremental consumers (e.g. visual row wrapping) can splice just
    // the affected range instead of rebuilding from scratch. Persists across
    // rebuilds so multiple consumers can each read it once per edit cycle.
    size_t lastRebuiltOldStart; // First old line index invalidated
    size_t lastRebuiltOldEnd; // Exclusive; old lines beyond this were reused as-is (shifted)
    size_t lastRebuiltNewEnd; // Exclusive; new lines beyond this are the reused/shifted tail
    bool lastRebuildOccurred; // Whether a rebuild has ever populated the fields above
} Buffer;

/**
 * @brief Creates a new Line object with the specified initial capacity.
 * @param initialCapacity Initial gap buffer capacity.
 * @return A pointer to the newly allocated Line, or NULL on failure.
 */
Line* Line_New(size_t initialCapacity);

/**
 * @brief Frees the Line object and its internal gap buffers.
 * @param line Pointer to the line.
 */
void Line_Free(Line* line);

/**
 * @brief Inserts a character at the specified byte position.
 * @param line Pointer to the line.
 * @param position Byte index to insert at.
 * @param character Character to insert.
 */
void Line_InsertChar(Line* line, size_t position, char character);

/**
 * @brief Deletes a character at the specified byte position.
 * @param line Pointer to the line.
 * @param position Byte index of the character to delete.
 */
void Line_DeleteChar(Line* line, size_t position);

/**
 * @brief Inserts a text block at the specified byte position.
 * @param line Pointer to the line.
 * @param position Byte index to insert at.
 * @param text Pointer to the characters.
 * @param length Number of characters to insert.
 */
void Line_InsertText(Line* line, size_t position, const char* text, size_t length);

/**
 * @brief Deletes a text block from the line.
 * @param line Pointer to the line.
 * @param position Byte index to start deleting.
 * @param length Number of characters to delete.
 */
void Line_DeleteText(Line* line, size_t position, size_t length);

/**
 * @brief Gets the logical character length of the line.
 * @param line Pointer to the line.
 * @return The number of bytes in the line.
 */
size_t Line_Length(Line* line);

/**
 * @brief Returns a Slice representing the line's text.
 * @param line Pointer to the line.
 * @return A slice of the line's text buffer.
 */
Slice Line_GetText(Line* line);

/**
 * @brief Returns a Slice covering just [start, start+length) of the line's
 * text, clamped to the line's actual length. Unlike Line_GetText, this never
 * materializes/caches the line's full content -- intended for rendering only
 * a visible sub-range of a huge line. The returned slice is only valid until
 * the next call to Line_GetTextRange (it may reuse an internal scratch buffer).
 * @param line Pointer to the line.
 * @param start Byte offset into the line to start at.
 * @param length Number of bytes to return.
 * @return A slice of at most `length` bytes.
 */
Slice Line_GetTextRange(Line* line, size_t start, size_t length);

/**
 * @brief Converts a logical byte position to a visual render column, accounting for tab stops.
 * @param line Pointer to the line.
 * @param cursorX Logical byte index.
 * @param tabSize The tab stop size configuration.
 * @return Visual render column index.
 */
size_t Line_GetRenderX(Line* line, size_t cursorX, size_t tabSize);

/**
 * @brief Creates a new empty text Buffer.
 * @return Pointer to the allocated Buffer, or NULL on failure.
 */
Buffer* Buffer_New(void);

/**
 * @brief Frees a Buffer and all of its Lines.
 * @param buffer Pointer to the buffer.
 */
void Buffer_Free(Buffer* buffer);

/**
 * @brief Inserts a new Line at the specified line number index.
 * @param buffer Pointer to the buffer.
 * @param lineNumber 0-based index to insert the line at.
 * @return Pointer to the newly inserted Line.
 */
Line* Buffer_InsertLine(Buffer* buffer, size_t lineNumber);

/**
 * @brief Deletes the line at the specified index.
 * @param buffer Pointer to the buffer.
 * @param lineNumber 0-based index of the line to delete.
 */
void Buffer_DeleteLine(Buffer* buffer, size_t lineNumber);

/**
 * @brief Gets the Line at the specified index.
 * @param buffer Pointer to the buffer.
 * @param lineNumber 0-based index.
 * @return Pointer to the Line, or NULL if out of bounds.
 */
Line* Buffer_GetLine(const Buffer* buffer, size_t lineNumber);

/**
 * @brief Inserts a character into the buffer at the exact line and column.
 * @param buffer Pointer to the buffer.
 * @param lineNumber 0-based line index.
 * @param column 0-based byte index within the line.
 * @param character Character to insert.
 */
void Buffer_InsertChar(Buffer* buffer, size_t lineNumber, size_t column, char character);

/**
 * @brief Deletes a character from the buffer at the exact line and column.
 * @param buffer Pointer to the buffer.
 * @param lineNumber 0-based line index.
 * @param column 0-based byte index.
 */
void Buffer_DeleteChar(Buffer* buffer, size_t lineNumber, size_t column);

/**
 * @brief Splits a line into two at the specified column.
 * @param buffer Pointer to the buffer.
 * @param lineNumber Index of the line to split.
 * @param column Column index at which to perform the split.
 */
void Buffer_SplitLine(Buffer* buffer, size_t lineNumber, size_t column);

/**
 * @brief Joins the line at lineNumber with the line directly below it.
 * @param buffer Pointer to the buffer.
 * @param lineNumber Index of the upper line.
 */
void Buffer_JoinLine(Buffer* buffer, size_t lineNumber);

/**
 * @brief Retrieves the total number of lines in the buffer.
 * @param buffer Pointer to the buffer.
 * @return Total line count.
 */
size_t Buffer_GetLineCount(const Buffer* buffer);

/**
 * @brief Returns the first line index marked dirty by a pending edit, without
 * triggering a line cache rebuild (unlike Buffer_GetLineCount/Buffer_GetLine).
 * @param buffer Pointer to the buffer.
 * @return The dirty line index, or SIZE_MAX if the cache is clean.
 */
size_t Buffer_PeekDirtyLineStart(const Buffer* buffer);

/**
 * @brief Returns the line range touched by the most recent line cache rebuild,
 * in both old (pre-edit) and new (post-edit) line-index space, so incremental
 * consumers (e.g. visual row wrapping) can splice just the affected range.
 * @param buffer Pointer to the buffer.
 * @param oldStart Out: first old line index invalidated by the rebuild.
 * @param oldEnd Out: exclusive end of the invalidated old-line range (old lines
 * at/after this index were reused as-is, just shifted).
 * @param newEnd Out: exclusive end of the freshly-computed new-line range (new
 * lines at/after this index are the reused/shifted tail).
 * @return True if a rebuild has occurred and the range is valid, false otherwise.
 */
bool Buffer_GetLastRebuildRange(const Buffer* buffer, size_t* oldStart, size_t* oldEnd, size_t* newEnd);

/**
 * @brief Computes the total exact byte size of the buffer when rendered into a single string.
 * @param buffer Pointer to the buffer.
 * @return Total exact bytes.
 */
size_t Buffer_GetTotalBytes(const Buffer* buffer);

/**
 * @brief Flattens the entire buffer into a single dynamically-allocated Slice.
 * The caller is responsible for freeing the `data` pointer inside the returned slice.
 * @param buffer Pointer to the buffer.
 * @return A Slice representing the full file content including inserted newlines.
 */
Slice Buffer_ToSlice(const Buffer* buffer);

/**
 * @brief Writes the buffer's content to `path` by streaming each piece
 * directly to a temp file (no intermediate full-document allocation) and
 * atomically renaming it into place on success. On failure, `path` is left
 * completely untouched.
 * @param buffer Pointer to the buffer.
 * @param path Destination path.
 * @return True on success, false on failure.
 */
bool Buffer_WriteToFileStreaming(const Buffer* buffer, const char* path);

/**
 * @brief Undoes the last action or action group.
 * @param buffer Pointer to the buffer.
 * @param outLineNumber Out: line the cursor should move to, if a group was undone.
 * @param outColumn Out: column the cursor should move to, if a group was undone.
 * @return True if a group was undone, false if the undo stack was empty.
 */
bool Buffer_Undo(Buffer* buffer, size_t* outLineNumber, size_t* outColumn);

/**
 * @brief Redoes the previously undone action or action group.
 * @param buffer Pointer to the buffer.
 * @param outLineNumber Out: line the cursor should move to, if a group was redone.
 * @param outColumn Out: column the cursor should move to, if a group was redone.
 * @return True if a group was redone, false if the redo stack was empty.
 */
bool Buffer_Redo(Buffer* buffer, size_t* outLineNumber, size_t* outColumn);
void Buffer_InsertText(Buffer* buffer, size_t pos, const char* text, size_t len);
void Buffer_DeleteRange(Buffer* buffer, size_t start, size_t end);

char Line_GetChar(Line* line, size_t index);
Buffer* Buffer_NewFromMmap(MappedFile mappedFile, const char* filename);
void Buffer_InvalidateLineCache(Buffer* buffer, size_t offset, size_t newEndOffset, size_t oldTotalBytes);
DocumentSnapshot* DocumentSnapshot_Copy(Buffer* buffer);
void DocumentSnapshot_Free(DocumentSnapshot* snap);
// Restores `snap` into `buffer`. `rangeStartOffset` is a byte offset, in the
// buffer's current (pre-restore) coordinate space, below which content is
// known to be unchanged between the current buffer and the restored
// snapshot - only the line cache from that point onward is invalidated,
// instead of the whole cache. Pass SIZE_MAX if no such bound is known, to
// fall back to discarding the entire line cache.
void Buffer_RestoreSnapshot(Buffer* buffer, DocumentSnapshot* snap, size_t rangeStartOffset);
void Buffer_OnSave(Buffer* buffer, const char* path);

// ============================================================================
// TERMINAL HANDLING
// ============================================================================

#define CTRL_KEY(k) ((k) & 0x1f)
#define TAB_STOP 4

enum Key {
    BACKSPACE = 127,
    ARROW_LEFT = 2000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    DELETE_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN,
    RESIZE_EVENT,
    ALT_S,
    ALT_F,
    CTRL_ARROW_LEFT,
    CTRL_ARROW_RIGHT,
    SHIFT_ARROW_UP,
    SHIFT_ARROW_DOWN,
    SHIFT_ARROW_LEFT,
    SHIFT_ARROW_RIGHT
};

extern volatile sig_atomic_t windowResized;

// Encapsulates terminal state
typedef struct Terminal {
    struct termios originalTermios;
    bool rawModeEnabled;
    bool termiosSaved;
} Terminal;

/**
 * @brief Enables raw mode for the terminal, disabling canonical input and echoing.
 * @param terminal Pointer to the terminal state.
 * @return True on success, false on failure.
 */
bool Terminal_EnableRawMode(Terminal* terminal);

/**
 * @brief Disables raw mode and restores the original terminal settings.
 * @param terminal Pointer to the terminal state.
 * @return True on success, false on failure.
 */
bool Terminal_DisableRawMode(Terminal* terminal);

/**
 * @brief Safely restores the terminal to its original state. Can be called multiple times.
 * @param terminal Pointer to the terminal state.
 * @return True on success, false on failure.
 */
bool Terminal_Restore(Terminal* terminal);

/**
 * @brief Clears the entire terminal screen using ANSI escape sequences.
 * @param terminal Pointer to the terminal state.
 */
void Terminal_ClearScreen(const Terminal* terminal);

/**
 * @brief Reads a single keypress or escape sequence from the terminal.
 * @return The integer key code (can be a standard char or an extended Key enum).
 */
int ReadKey(void);

/**
 * @brief Queries the terminal for the current cursor position.
 * @param rows Pointer to store the row coordinate.
 * @param columns Pointer to store the column coordinate.
 * @return True on success, false on failure.
 */
bool Terminal_GetCursorPosition(size_t* rows, size_t* columns);

/**
 * @brief Retrieves the current dimensions of the terminal window.
 * @param rows Pointer to store the number of rows.
 * @param columns Pointer to store the number of columns.
 * @return True on success, false on failure.
 */
bool Terminal_GetWindowSize(size_t* rows, size_t* columns);

/**
 * @brief Signal handler callback for graceful terminal cleanup on exit/crash.
 * @param terminal Pointer to the terminal state.
 * @param signalNumber The signal received (e.g., SIGINT, SIGSEGV).
 */
void Terminal_HandleSignal(Terminal* terminal, int signalNumber);

// ============================================================================
// FILE I/O
// ============================================================================

/**
 * @brief Reads the entire contents of a file into a dynamic array.
 * Uses memory-mapping for files >1MB to optimize performance.
 * @param path Absolute or relative path to the file.
 * @param out Pointer to an Array to store the contents.
 * @return True on success, false on failure.
 */
bool FileIoRead(const char* path, Array* out);

/**
 * @brief Writes a full slice of data to a file, truncating any existing content.
 * Not atomic - a crash or write failure partway through can leave the file
 * truncated/corrupted. Prefer FileIoOpenTempForAtomicWrite + FileIoWriteChunk +
 * FileIoCommitAtomicWrite for saving over an existing file safely.
 * @param path Path to write to.
 * @param content The slice containing the data to write.
 * @return True on success, false on failure.
 */
bool FileIoWrite(const char* path, Slice content);

/**
 * @brief Writes exactly `len` bytes to an open file descriptor, looping over
 * short writes and retrying on EINTR.
 * @param fileDescriptor Open, writable file descriptor.
 * @param data Bytes to write.
 * @param len Number of bytes to write.
 * @return True if all bytes were written, false on error.
 */
bool FileIoWriteChunk(int fileDescriptor, const void* data, size_t len);

/**
 * @brief Opens a new temp file in the same directory as `path` (so a later
 * rename() onto `path` is atomic), for streaming content into before
 * committing it as the new content of `path`.
 * @param path Final destination path the temp file will eventually replace.
 * @param tempPathOut Buffer to receive the temp file's path.
 * @param tempPathOutSize Size of tempPathOut.
 * @return An open, writable file descriptor, or -1 on failure.
 */
int FileIoOpenTempForAtomicWrite(const char* path, char* tempPathOut, size_t tempPathOutSize);

/**
 * @brief Abandons an in-progress atomic write: closes the descriptor and
 * removes the temp file, leaving the original destination file untouched.
 * @param fileDescriptor The temp file's descriptor (may be -1 if already closed).
 * @param tempPath The temp file's path, as returned by FileIoOpenTempForAtomicWrite.
 */
void FileIoAbortAtomicWrite(int fileDescriptor, const char* tempPath);

/**
 * @brief Durably commits a completed atomic write: fsyncs and closes the temp
 * file, then atomically renames it onto `finalPath`. On failure the temp file
 * is removed and `finalPath` is left untouched.
 * @param fileDescriptor The temp file's descriptor, as returned by FileIoOpenTempForAtomicWrite.
 * @param tempPath The temp file's path.
 * @param finalPath The destination path to atomically replace.
 * @return True on success, false on failure.
 */
bool FileIoCommitAtomicWrite(int fileDescriptor, const char* tempPath, const char* finalPath);

// Memory-mapped file variant (zero-copy for large files)

/**
 * @brief Memory-maps a file for zero-copy read access.
 * @param path Path to the file.
 * @return A MappedFile struct containing the slice and file descriptor.
 */
MappedFile FileIoMmap(const char* path);

/**
 * @brief Unmaps a memory-mapped file and closes its descriptor.
 * @param file Pointer to the MappedFile struct.
 */
void MappedFile_Unmap(MappedFile* file);

// ============================================================================
// SYNTAX HIGHLIGHTING
// ============================================================================

typedef enum {
    HIGHLIGHT_NORMAL = 0,
    HIGHLIGHT_NUMBER,
    HIGHLIGHT_MATCH,
    HIGHLIGHT_STRING,
    HIGHLIGHT_CHARACTER,
    HIGHLIGHT_COMMENT,
    HIGHLIGHT_KEYWORD,
    HIGHLIGHT_TYPE,
    HIGHLIGHT_SYMBOL
} HighlightType;

typedef struct {
    char* fileType;
    char** fileMatch;
    char** keywords;
    char** types;
    char* singleLineCommentStart;
    char* multiLineCommentStart;
    char* multiLineCommentEnd;
} Syntax;

// ============================================================================
// EDITOR FEATURES & APP STATE
// ============================================================================

typedef struct {
    char* name;
    bool isDir;
    mode_t mode;
    off_t size;
    time_t mtime;
} ExplorerItem;

typedef struct {
    int tabSize;
    bool showLineNumbers;
    bool wrapLines;
    // Auto-disables word-wrap for a file whose line count exceeds this, since
    // wrapping requires an eager full-document pass to materialize every
    // line's text and compute wrap segments. 0 disables this override
    // entirely (always respect wrapLines regardless of file size).
    size_t wrapDisableLineThreshold;
    bool syntaxEnabled;
    int statusTimeout;
    char* syntaxColors[9]; // Map of HighlightType enum
    Array syntaxDatabase; // Dynamic Array of Syntax

    // Keybindings
    int keySave;
    int keyQuit;
    int keyNewTab;
    int keyCloseTab;
    int keyExplorer;
    int keyNextTab;
    int keyPrevTab;
    int keySaveAs;
    int keyLogs;
    int keyToggleFold;
    int keyToggleAllFolds;

    // Logging Configuration
    char* logFile;
    char* logLevelStr;
    bool logToFile;
    bool logToUi;
    int logMaxMessages;

    // Undo Configuration
    size_t undoLimit;
} Config;

typedef struct {
    // Terminal state
    Terminal terminal;
    size_t screenRows;
    size_t screenColumns;

    // Status message state
    char statusMessage[200];
    time_t statusMessageTime;

    // Tabs state
    Array tabs; // Array of Tab*
    size_t activeTabIndex;

    // Explorer State
    bool isExplorerActive;
    Array explorerItems; // Array of dynamically allocated ExplorerItem pointers (ExplorerItem*)
    size_t explorerSelectedIndex;
    char currentExplorerPath[512];

    // Logs State
    bool isLogsActive;
    size_t logsSelectedIndex;

    // Configuration
    Config config;
} Editor;

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

/**
 * @brief Initializes the global Editor application state.
 * @param editor Pointer to the Editor.
 */
void Editor_Init(Editor* editor);

/**
 * @brief Frees the Editor application state and all of its tabs.
 * @param editor Pointer to the Editor.
 */
void Editor_Free(Editor* editor);

/**
 * @brief Enables raw mode for the terminal.
 * @param editor Pointer to the Editor.
 * @return True on success, false on failure.
 */
bool Editor_InitTerminal(Editor* editor);

/**
 * @brief Restores original terminal settings.
 * @param editor Pointer to the Editor.
 */
void Editor_RestoreTerminal(Editor* editor);

/**
 * @brief Opens a new tab, optionally loading a file into it.
 * @param editor Pointer to the Editor.
 * @param filename File path to load, or NULL for an empty buffer.
 */
void Editor_AddTab(Editor* editor, const char* filename);

/**
 * @brief Closes the currently active tab.
 * @param editor Pointer to the Editor.
 */
void Editor_CloseTab(Editor* editor);

/**
 * @brief Processes the next keypress for the active tab or explorer.
 * @param editor Pointer to the Editor.
 */
void Editor_ProcessKeypress(Editor* editor);

/**
 * @brief Recalculates screen dimensions and distributes them across tabs.
 * @param editor Pointer to the Editor.
 */
void Editor_UpdateGeometry(Editor* editor);

/**
 * @brief Re-renders the entire terminal screen.
 * @param editor Pointer to the Editor.
 */
void Editor_RefreshScreen(Editor* editor);

/**
 * @brief Sets a formatted status message to be displayed in the message bar.
 * @param editor Pointer to the Editor.
 * @param fstring Format string (printf style).
 * @param ... Additional arguments.
 */
void Editor_SetStatusMessage(Editor* editor, const char* fstring, ...);

/**
 * @brief Renders the message bar at the bottom of the screen.
 * @param editor Pointer to the Editor.
 * @param screenBuffer Render buffer.
 */
void Editor_DrawMessageBar(Editor* editor, Array* screenBuffer);

/**
 * @brief Renders the visible rows of the active tab to the screen.
 * @param editor Pointer to the Editor.
 * @param screenBuffer Render buffer.
 */
void Editor_DrawTabRows(Editor* editor, Array* screenBuffer);

/**
 * @brief Renders the status bar (filename, line count, position) for the active tab.
 * @param editor Pointer to the Editor.
 * @param screenBuffer Render buffer.
 */
void Editor_DrawStatusBar(Editor* editor, Array* screenBuffer);

/**
 * @brief Prompts the user for input via the message bar.
 * @param editor Pointer to the Editor.
 * @param prompt Format string to display.
 * @return Dynamically allocated string with the user's input.
 */
char* Editor_Prompt(Editor* editor, const char* prompt);

// ============================================================================
// EXPLORER FUNCTIONS (Editor namespace acting on Editor)
// ============================================================================

/**
 * @brief Reads a directory and populates the ExplorerItems list.
 * @param editor Pointer to the Editor.
 * @param path Path to the directory to read.
 */
void Editor_ReadDir(Editor* editor, const char* path);

/**
 * @brief Renders the File Explorer interface.
 * @param editor Pointer to the Editor.
 * @param screenBuffer The render buffer to append ANSI sequences to.
 */
void Editor_DrawExplorer(Editor* editor, Array* screenBuffer);

/**
 * @brief Processes a keypress while the File Explorer is active.
 * @param editor Pointer to the Editor.
 * @param input The keypress code.
 */
void Editor_ProcessExplorerInput(Editor* editor, int input);

// ============================================================================
// TAB FUNCTIONS
// ============================================================================

/**
 * @brief Initializes a new Tab.
 * @param tab Pointer to the Tab.
 */
void Tab_Init(Tab* tab);

/**
 * @brief Frees the resources associated with a Tab.
 * @param tab Pointer to the Tab.
 */
void Tab_Free(Tab* tab);

/**
 * @brief Loads a file from disk into the Tab's buffer.
 * @param tab Pointer to the Tab.
 * @param path Path to the file.
 */
void Tab_LoadFile(Tab* tab, const char* path);

/**
 * @brief Saves the Tab's buffer back to disk.
 * @param tab Pointer to the Tab.
 */
void Tab_SaveFile(Tab* tab);

/**
 * @brief Whether this tab should currently word-wrap its lines, combining the
 * global config->wrapLines setting with this tab's per-file size override
 * (see Tab.wrapLinesDisabledForSize).
 * @param tab Pointer to the Tab.
 * @return True if lines should be word-wrapped.
 */
bool Tab_ShouldWrapLines(const Tab* tab);

/**
 * @brief Triggers a syntax highlight update for the entire tab.
 * @param tab Pointer to the Tab.
 */
void Tab_SetSyntaxHighlight(Tab* tab);

/**
 * @brief Re-evaluates syntax highlighting, extending coverage as needed.
 * If a pending edit exists, re-highlights from the touched line forward
 * (bounded by the usual early-exit once comment state restabilizes).
 * Otherwise, lazily extends coverage from the current high-water mark up to
 * `maxLine` (a no-op if that range is already covered) -- pass SIZE_MAX to
 * always cover the whole buffer regardless of viewport.
 * @param tab Pointer to the Tab.
 * @param maxLine Exclusive upper bound on lines to newly highlight.
 */
void Tab_UpdateSyntax(Tab* tab, size_t maxLine);

/**
 * @brief Retrieves the ANSI color code for a specific highlight type.
 * @param config Pointer to the Config object.
 * @param highlightType The highlight enum type.
 * @return An ANSI escape sequence color string.
 */
char* GetSyntaxColor(const Config* config, HighlightType highlightType);

/**
 * @brief Loads the Lua configuration file from disk.
 * @param editor Pointer to the Editor instance.
 * @param configFilePath File path to load.
 * @return True if configuration loaded successfully, false otherwise.
 */
bool Editor_LoadConfig(Editor* editor, const char* configFilePath);

/**
 * @brief Initializes a Config object with default settings.
 * @param config Pointer to the Config instance.
 */
void Config_InitDefaults(Config* config);

/**
 * @brief Frees all dynamic memory associated with a Config object.
 * @param config Pointer to the Config instance.
 */
void Config_Free(Config* config);

/**
 * @brief Computes the number of digits required for line numbers.
 * @param tab Pointer to the Tab.
 * @return Number of digits (minimum 3).
 */
size_t Tab_GetGutterDigits(const Tab* tab);

/**
 * @brief Computes the total visual width of the line number gutter.
 * @param tab Pointer to the Tab.
 * @return Visual column width.
 */
size_t Tab_GetGutterWidth(const Tab* tab);

/**
 * @brief Retrieves the visual row index of the cursor.
 * @param tab Pointer to the Tab.
 * @return Visual row index.
 */
size_t Tab_GetCursorVRowIdx(const Tab* tab);

/**
 * @brief Retrieves the total number of visual rows, optimized for unwrapped/unfolded text.
 * @param tab Pointer to the Tab.
 * @return Total number of visual rows.
 */
size_t Tab_GetVisualRowCount(const Tab* tab);


/**
 * @brief Retrieves the visual column offset of the cursor in a specific visual row.
 * @param tab Pointer to the Tab.
 * @param vrowIdx Visual row index.
 * @return Visual column offset.
 */
size_t Tab_GetCursorVisualCol(const Tab* tab, size_t vrowIdx);

/**
 * @brief Sets the logical cursor position based on visual row and target visual column.
 * @param tab Pointer to the Tab.
 * @param targetVRowIdx Target visual row index.
 * @param targetVisualCol Target visual column offset.
 */
void Tab_SetCursorFromVRow(Tab* tab, size_t targetVRowIdx, size_t targetVisualCol);

/**
 * @brief Re-evaluates visual wrapping segments for the entire buffer based on width.
 * @param editor Pointer to the Editor.
 * @param tab Pointer to the Tab.
 * @param usableColumns Maximum columns available for rendering text.
 */
void Tab_UpdateVisualRows(const Editor* editor, Tab* tab, size_t usableColumns);

/**
 * @brief Calculates scroll offsets to ensure the cursor remains visible.
 * @param editor Pointer to the Editor.
 * @param tab Pointer to the Tab.
 */
void Editor_ScrollTab(Editor* editor, Tab* tab);

/**
 * @brief Gets the exact start and end coordinates of the active text selection.
 * @param tab Pointer to the Tab.
 * @param startX Pointer to store the starting column.
 * @param startY Pointer to store the starting row.
 * @param endX Pointer to store the ending column.
 * @param endY Pointer to store the ending row.
 */
void Tab_GetSelection(Tab* tab, size_t* startX, size_t* startY, size_t* endX, size_t* endY);

// ============================================================================
// TEXT EDITING OPERATIONS (Editor namespace acting on Editor)
// ============================================================================

/**
 * @brief Moves the cursor within the current active tab buffer based on arrow key input.
 * @param editor Pointer to the Editor.
 * @param key The arrow key pressed.
 */
void Editor_MoveCursor(Editor* editor, int key);

/**
 * @brief Moves the cursor word-by-word.
 * @param editor Pointer to the Editor.
 * @param key The arrow key pressed.
 */
void Editor_MoveCursorWord(Editor* editor, int key);

/**
 * @brief Deletes the currently selected text.
 * @param editor Pointer to the Editor.
 */
void Editor_DeleteSelection(Editor* editor);

/**
 * @brief Processes input character or deletion/insertion in the active tab buffer.
 * @param editor Pointer to the Editor.
 * @param input The keypress/character.
 */
void Editor_ProcessInput(Editor* editor, int input);

/**
 * @brief Toggles folding state for the current line.
 * @param editor Pointer to the Editor.
 */
void Editor_ToggleFold(Editor* editor);

/**
 * @brief Toggles folding state for all foldable blocks in the file.
 * @param editor Pointer to the Editor.
 */
void Editor_ToggleAllFolds(Editor* editor);

/**
 * @brief Checks if a line is foldable based on indentation levels.
 * @param buffer Pointer to the Buffer.
 * @param lineNumber Index of the line to check.
 * @param tabSize Tab stop size configuration.
 * @return True if foldable, false otherwise.
 */
bool Line_IsFoldable(const Buffer* buffer, size_t lineNumber, size_t tabSize);

/**
 * @brief Calculates indentation width of a line, expanding tab stops.
 * @param line Pointer to the Line.
 * @param tabSize Tab stop size configuration.
 * @return The indentation size.
 */
size_t Line_GetIndentation(Line* line, size_t tabSize);

/**
 * @brief Checks if a line consists entirely of whitespace.
 * @param line Pointer to the Line.
 * @return True if blank, false otherwise.
 */
bool Line_IsBlank(Line* line);

/**
 * @brief Checks if a logical line index is currently visible on screen.
 * @param tab Pointer to the Tab.
 * @param lineIndex Index of the line.
 * @return True if visible, false otherwise.
 */
bool Tab_IsLineVisible(const Tab* tab, size_t lineIndex);

/**
 * @brief Finds the next visible line index starting from the given line.
 * @param tab Pointer to the Tab.
 * @param lineIndex Index of the line.
 * @return The next visible line index.
 */
size_t Tab_NextVisibleLine(const Tab* tab, size_t lineIndex);

/**
 * @brief Finds the previous visible line index starting from the given line.
 * @param tab Pointer to the Tab.
 * @param lineIndex Index of the line.
 * @return The previous visible line index.
 */
size_t Tab_PrevVisibleLine(const Tab* tab, size_t lineIndex);

// ============================================================================
// CLI ARGUMENT HANDLING
// ============================================================================

typedef struct {
    const char* configPath;
    int overrideTabSize;
    int overrideShowLineNumbers; // -1: no override, 0: false, 1: true
    int overrideWrapLines; // -1: no override, 0: false, 1: true
    int overrideSyntaxEnabled; // -1: no override, 0: false, 1: true
    bool readOnlyMode;

    // Logging Overrides
    const char* overrideLogFile;
    const char* overrideLogLevel;
    int overrideLogToFile; // -1: no override, 0: false, 1: true
    int overrideLogToUi; // -1: no override, 0: false, 1: true
    int overrideLogMaxMessages;

    char** files;
    int* fileLines;
    int* fileColumns;
    int fileCount;

    bool helpRequested;
    bool versionRequested;
} CliOptions;

/**
 * @brief Parses command line arguments.
 * @param options Pointer to the CliOptions struct to populate.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return true on success, false if parsing fails or invalid options.
 */
bool CliOptions_Parse(CliOptions* options, int argc, char* argv[]);

/**
 * @brief Frees any resources allocated inside a CliOptions struct.
 * @param options Pointer to the CliOptions struct.
 */
void CliOptions_Free(CliOptions* options);

/**
 * @brief Prints usage and help screen.
 * @param progName The program name (argv[0]).
 */
void PrintHelp(const char* progName);

/**
 * @brief Prints program version information.
 */
void PrintVersion(void);

// ============================================================================
// LOGGING SYSTEM
// ============================================================================

typedef enum { LOG_LEVEL_DEBUG = 0, LOG_LEVEL_INFO, LOG_LEVEL_WARN, LOG_LEVEL_ERROR, LOG_LEVEL_FATAL } LogLevel;

/**
 * @brief Initializes the logging system.
 * @param logFile Path to the log file.
 * @param level Logging level threshold.
 * @param logToFile True to enable writing to the log file.
 * @param logToUi True to enable keeping logs in memory for the UI.
 * @param maxMessages Maximum number of messages to store in memory.
 */
void Logger_Init(const char* logFile, LogLevel level, bool logToFile, bool logToUi, int maxMessages);

/**
 * @brief Reconfigures the logging system.
 * @param logFile Path to the log file.
 * @param level Logging level threshold.
 * @param logToFile True to enable writing to the log file.
 * @param logToUi True to enable keeping logs in memory for the UI.
 * @param maxMessages Maximum number of messages to store in memory.
 */
void Logger_Configure(const char* logFile, LogLevel level, bool logToFile, bool logToUi, int maxMessages);

/**
 * @brief Frees resources allocated by the logging system.
 */
void Logger_Free(void);

/**
 * @brief Logs a formatted message.
 * @param level Severity level.
 * @param file Source file name.
 * @param line Source line number.
 * @param format Format string.
 */
void Logger_Log(LogLevel level, const char* file, int line, const char* format, ...);

/**
 * @brief Returns the in-memory array of log messages (Array of char*).
 * @return Pointer to the log messages Array.
 */
Array* Logger_GetMessages(void);

/**
 * @brief Parses a string log level to its LogLevel enum value.
 * @param levelStr String representations.
 * @param defaultLevel Default fallback value.
 * @return The parsed LogLevel enum.
 */
LogLevel Logger_ParseLevel(const char* levelStr, LogLevel defaultLevel);

/**
 * @brief Converts a LogLevel enum to its string representation.
 * @param level LogLevel enum value.
 * @return String representation of the level.
 */
const char* Logger_LevelToString(LogLevel level);

// Logging Macros
#define LOG_DEBUG(...) Logger_Log(LOG_LEVEL_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_INFO(...) Logger_Log(LOG_LEVEL_INFO, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_WARN(...) Logger_Log(LOG_LEVEL_WARN, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...) Logger_Log(LOG_LEVEL_ERROR, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_FATAL(...) Logger_Log(LOG_LEVEL_FATAL, __FILE__, __LINE__, __VA_ARGS__)

// UI Logs overlay functions
void Editor_ToggleLogs(Editor* editor);
void Editor_DrawLogs(Editor* editor, Array* screenBuffer);
void Editor_ProcessLogsInput(Editor* editor, int input);
