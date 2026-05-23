#pragma once

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <signal.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stddef.h>
#include <termios.h>
#include <time.h>

// ============================================================================
// CORE DATA STRUCTURES
// ============================================================================

// Slice - Type-agnostic slice/string view
typedef struct {
    const void* data;
    size_t size;
} Slice;

// Creation
Slice Slice_Make(const void* data, size_t size);
Slice Slice_From(const void* string);

// Comparison
bool Slice_Equals(Slice a, Slice b);

// Utility
Slice Slice_Subslice(Slice slice, size_t start, size_t end);

// Array - Type-agnostic arraylist
typedef struct {
    void* data;
    size_t size;
    size_t capacity;
    size_t itemSize;
    size_t alignment;
} Array;

// Life cycle
void Array_Init(Array* array, size_t itemSize, size_t capacity, size_t alignment);
void Array_Free(Array* array);
void Array_Clear(Array* array);

// Operation
bool Array_Append(Array* array, const void* items, size_t count);
bool Array_Pop(Array* array);

// Access
void* Array_At(const Array* array, size_t index); // Bounds-checked by logical size
void* Array_RawAt(const Array* array, size_t index); // Bounds-checked by capacity; for internal data structures
size_t Array_Size(const Array* array);
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

// Initialize with initial capacity
void GapBuffer_Init(GapBuffer* gapBuffer, size_t initialCapacity, size_t alignment);

// Free resources
void GapBuffer_Free(GapBuffer* gapBuffer);

// Core operations
void GapBuffer_InsertSlice(GapBuffer* gapBuffer, size_t position, Slice content);
void GapBuffer_InsertChar(GapBuffer* gapBuffer, size_t position, char content);
void GapBuffer_Delete(GapBuffer* gapBuffer, size_t position, size_t size);
Slice GapBuffer_ToSlice(GapBuffer* gapBuffer); // Get all content as slice
void GapBuffer_Clear(GapBuffer* gapBuffer);

// Utility
size_t GapBuffer_Size(const GapBuffer* gapBuffer);
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

// Line operations
Line* Line_New(size_t initialCapacity);
void Line_Free(Line* line);
void Line_InsertChar(Line* line, size_t position, char character);
void Line_DeleteChar(Line* line, size_t position);
void Line_InsertText(Line* line, size_t position, const char* text, size_t length);
void Line_DeleteText(Line* line, size_t position, size_t length);
size_t Line_Length(Line* line);
Slice Line_GetText(Line* line);
// Converts a byte-position cursor to its visual render column, accounting for tabs
size_t Line_GetRenderX(Line* line, size_t cursorX);

// Buffer operations
Buffer* Buffer_New(void);
void Buffer_Free(Buffer* buffer);
Line* Buffer_InsertLine(Buffer* buffer, size_t lineNumber);
void Buffer_DeleteLine(Buffer* buffer, size_t lineNumber);
Line* Buffer_GetLine(const Buffer* buffer, size_t lineNumber);
void Buffer_InsertChar(Buffer* buffer, size_t lineNumber, size_t column, char character);
void Buffer_DeleteChar(Buffer* buffer, size_t lineNumber, size_t column);
void Buffer_SplitLine(Buffer* buffer, size_t lineNumber, size_t column);
void Buffer_JoinLine(Buffer* buffer, size_t lineNumber);
size_t Buffer_GetLineCount(const Buffer* buffer);
size_t Buffer_GetTotalBytes(const Buffer* buffer);
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
    RESIZE_EVENT
};

extern volatile sig_atomic_t windowResized;

// Encapsulates terminal state
typedef struct Terminal {
    struct termios originalTermios;
    bool rawModeEnabled;
    bool termiosSaved;
} Terminal;

// Enables raw mode. Returns 0 on success, -1 on failure.
bool Terminal_EnableRawMode(Terminal* terminal);

// Disables raw mode. Returns 0 on success, -1 on failure.
bool Terminal_DisableRawMode(Terminal* terminal);

// Restores the terminal to its original state. Safe to call multiple times.
bool Terminal_Restore(Terminal* terminal);

// Clears the terminal screen using ANSI escape codes.
void Terminal_ClearScreen(const Terminal* terminal);

// Reads a single key from the terminal. Raw mode must be enabled. Returns the
// character read, or -1 on error.
int ReadKey(void);

// Gets the terminal cursor position
bool Terminal_GetCursorPosition(size_t* rows, size_t* columns);

// Gets the terminal window size
bool Terminal_GetWindowSize(size_t* rows, size_t* columns);

// Handles signals for terminal cleanup. Intended for use as a signal handler.
void Terminal_HandleSignal(Terminal* terminal, int signalNumber);

// ============================================================================
// FILE I/O
// ============================================================================

// Read entire file into array (uses memory mapping for large files)
bool FileIoRead(const char* path, Array* out);

// Write slice to file (atomic write on POSIX)
bool FileIoWrite(const char* path, Slice content);

// Memory-mapped file variant (zero-copy for large files)
typedef struct {
    Slice content;
    int fileDescriptor; // File descriptor for cleanup
} MappedFile;

MappedFile FileIoMmap(const char* path);
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
// EDITOR FEATURES
// ============================================================================

typedef struct {
    size_t screenRows;
    size_t screenColumns;
    char statusMessage[200];
    time_t statusMessageTime;
} Editor;

typedef struct {
    // Text buffer
    Buffer* buffer;

    // Editor state
    Editor* editor;

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
} Tab;

void Editor_Init(Editor* editor);
void Editor_Free(Editor* editor);
void Editor_SetStatusMessage(Editor* editor, const char* fstring, ...);
void Editor_DrawMessageBar(Editor* editor, Array* screenBuffer);

// ============================================================================
// APP STATE
// ============================================================================

typedef struct {
    Array tabs;
    size_t activeTabIndex;
} App;

void App_Init(App* app);
void App_Free(App* app);
void App_AddTab(App* app, const char* filename);
void App_CloseTab(App* app);
void App_ProcessKeypress(App* app);
void App_UpdateGeometry(App* app);
void App_RefreshScreen(App* app);

void Tab_Init(Tab* tab);
void Tab_Free(Tab* tab);
void Tab_LoadFile(Tab* tab, const char* path);
void Tab_SaveFile(Tab* tab);

void Tab_SetSyntaxHighlight(Tab* tab);
void Tab_UpdateSyntax(Tab* tab);
char* GetSyntaxColor(HighlightType highlight);

// ============================================================================
// TERMINAL RENDERING
// ============================================================================

void Tab_Scroll(Tab* tab);
void Tab_DrawRows(Tab* tab, Array* screenBuffer);
void Tab_DrawStatusBar(Tab* tab, Array* screenBuffer);

// ============================================================================
// KEYBOARD INPUT
// ============================================================================

void Tab_MoveCursor(Tab* tab, int key);
void Tab_ProcessInput(Tab* tab, int input);
