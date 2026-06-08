#pragma once

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

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

// GapBuffer - Gap buffer with internal Array of char type
typedef struct {
    Array data;
    size_t gapStart;
    size_t gapEnd;
} GapBuffer;

/**
 * @brief Initializes a gap buffer.
 * @param gapBuffer Pointer to the gap buffer to initialize.
 * @param initialCapacity Starting capacity.
 * @param alignment Memory alignment constraint.
 */
void GapBuffer_Init(GapBuffer* gapBuffer, size_t initialCapacity, size_t alignment);

/**
 * @brief Frees resources associated with the gap buffer.
 * @param gapBuffer Pointer to the gap buffer.
 */
void GapBuffer_Free(GapBuffer* gapBuffer);

/**
 * @brief Inserts a slice of data at a logical position in the gap buffer.
 * @param gapBuffer Pointer to the gap buffer.
 * @param position Logical index to insert at.
 * @param content The slice of content to insert.
 */
void GapBuffer_InsertSlice(GapBuffer* gapBuffer, size_t position, Slice content);

/**
 * @brief Inserts a single character at a logical position.
 * @param gapBuffer Pointer to the gap buffer.
 * @param position Logical index to insert at.
 * @param content Character to insert.
 */
void GapBuffer_InsertChar(GapBuffer* gapBuffer, size_t position, char content);

/**
 * @brief Deletes a sequence of bytes from the gap buffer.
 * @param gapBuffer Pointer to the gap buffer.
 * @param position Logical index to start deleting.
 * @param size Number of bytes to delete.
 */
void GapBuffer_Delete(GapBuffer* gapBuffer, size_t position, size_t size);

/**
 * @brief Flattens the gap buffer into a dynamically allocated contiguous slice.
 * @param gapBuffer Pointer to the gap buffer.
 * @return A Slice containing the complete content.
 */
Slice GapBuffer_ToSlice(GapBuffer* gapBuffer);

/**
 * @brief Clears the entire gap buffer.
 * @param gapBuffer Pointer to the gap buffer.
 */
void GapBuffer_Clear(GapBuffer* gapBuffer);

/**
 * @brief Gets a character at a specific logical position in the gap buffer.
 * @param gapBuffer Pointer to the gap buffer.
 * @param index Logical index.
 * @return The character at the index.
 */
char GapBuffer_Get(const GapBuffer* gapBuffer, size_t index);

/**
 * @brief Returns the total logical size (amount of text/content) in the gap buffer.
 * @param gapBuffer Pointer to the gap buffer.
 * @return Logical size in bytes.
 */
size_t GapBuffer_Size(const GapBuffer* gapBuffer);
/**
 * @brief Moves the gap to the specified new logical position.
 * @param gapBuffer Pointer to the gap buffer.
 * @param newGapStart The new start position for the gap.
 */
void GapBuffer_MoveGap(GapBuffer* gapBuffer, size_t newGapStart);

// ============================================================================
// LINKED LIST TEXT BUFFER
// ============================================================================

// Line - Represents a single line in the text buffer
typedef struct Line {
    GapBuffer text; // Text content of the line
    GapBuffer styles; // Parallel gap buffer for syntax highlighting
    size_t lineNumber; // 0-based line number
    bool isFolded; // Whether this line is folded/collapsed
    size_t foldLevel; // Nesting level for folding
    struct Line* next; // Next line in the list
    struct Line* prev; // Previous line in the list
} Line;

// Buffer - Main text buffer containing linked list of lines
typedef struct Buffer {
    Line* firstLine; // First line in the buffer
    Line* lastLine; // Last line in the buffer
    Line* currentLine; // Currently active line
    size_t totalLines; // Total number of lines
    size_t totalBytes; // Total number of bytes across all lines
    char* filename; // Associated filename
    bool isModified; // Whether buffer has been modified
    bool isReadOnly; // Whether buffer is read-only
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
 * @brief Writes a slice of data atomically to a file.
 * @param path Path to write to.
 * @param content The slice containing the data to write.
 * @return True on success, false on failure.
 */
bool FileIoWrite(const char* path, Slice content);

// Memory-mapped file variant (zero-copy for large files)
typedef struct {
    Slice content;
    int fileDescriptor; // File descriptor for cleanup
} MappedFile;

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
 * @brief Triggers a syntax highlight update for the entire tab.
 * @param tab Pointer to the Tab.
 */
void Tab_SetSyntaxHighlight(Tab* tab);

/**
 * @brief Re-evaluates syntax highlighting for the current lines.
 * @param tab Pointer to the Tab.
 */
void Tab_UpdateSyntax(Tab* tab);

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
