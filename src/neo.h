#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <termios.h>

// ============================================================================
// CORE DATA STRUCTURES
// ============================================================================

// Slice - Type-agnostic slice/string view
typedef struct {
  const void *data;
  size_t size;
} Slice;

// Creation
Slice Slice_Make(const void *data, size_t size);
Slice Slice_From(const void *str);

// Comparison
bool Slice_Equals(Slice a, Slice b);

// Utility
Slice Slice_Subslice(Slice slice, size_t start, size_t end);

// Array - Type-agnostic arraylist
typedef struct {
  void *data;
  size_t size;
  size_t capacity;
  size_t itemSize;
  size_t alignment;
} Array;

// Life cycle
void Array_Init(Array *array, size_t itemSize, size_t capacity,
                size_t alignment);
void Array_Free(Array *array);
void Array_Clear(Array *array);

// Operation
bool Array_Append(Array *array, const void *items, size_t count);
bool Array_Pop(Array *array);

// Access
void *Array_At(const Array *array, size_t index);
size_t Array_Size(const Array *array);
Slice Array_ToSlice(const Array *array);

// Type-safe macros
#define Array_InitChar(arr, cap)                                               \
  Array_Init(arr, sizeof(char), cap, 64) // Cache line
#define Array_InitStruct(arr, type, cap)                                       \
  Array_Init(arr, sizeof(type), cap, alignof(type))
#define Array_Get(arr, type, idx) (*(type *)Array_At(arr, idx))
#define Array_AppendSlice(arr, slice) Array_Append(arr, slice.data, slice.size)

// GapBuffer - Gap buffer with internal Array of char type
typedef struct {
  Array data;
  size_t gapStart;
  size_t gapEnd;
} GapBuffer;

// Initialize with initial capacity
void GapBuffer_Init(GapBuffer *gapBuffer, size_t initialCapacity,
                    size_t alignment);

// Free resources
void GapBuffer_Free(GapBuffer *gapBuffer);

// Core operations
void GapBuffer_InsertSlice(GapBuffer *gapBuffer, size_t position,
                           Slice content);
void GapBuffer_InsertChar(GapBuffer *gapBuffer, size_t position, char content);
void GapBuffer_Delete(GapBuffer *gapBuffer, size_t position, size_t size);
Slice GapBuffer_ToSlice(GapBuffer *gapBuffer); // Get all content as slice
void GapBuffer_Clear(GapBuffer *gapBuffer);

// Utility
size_t GapBuffer_Size(const GapBuffer *gapBuffer);
void GapBuffer_MoveGap(GapBuffer *gapBuffer, size_t newGapStart);

// ============================================================================
// LINKED LIST TEXT BUFFER
// ============================================================================

// Line - Represents a single line in the text buffer
typedef struct Line {
  GapBuffer text;    // Text content of the line
  GapBuffer styles;  // Parallel gap buffer for syntax highlighting
  size_t lineNumber; // 0-based line number
  bool isFolded;     // Whether this line is folded/collapsed
  size_t foldLevel;  // Nesting level for folding
  struct Line *next; // Next line in the list
  struct Line *prev; // Previous line in the list
} Line;

// Buffer - Main text buffer containing linked list of lines
typedef struct Buffer {
  Line *firstLine;   // First line in the buffer
  Line *lastLine;    // Last line in the buffer
  Line *currentLine; // Currently active line
  size_t totalLines; // Total number of lines
  size_t totalBytes; // Total number of bytes across all lines
  char *filename;    // Associated filename
  bool isModified;   // Whether buffer has been modified
  bool isReadOnly;   // Whether buffer is read-only
} Buffer;

// Line operations
Line *Line_New(size_t initialCapacity);
void Line_Free(Line *line);
void Line_InsertChar(Line *line, size_t position, char c);
void Line_DeleteChar(Line *line, size_t position);
void Line_InsertText(Line *line, size_t position, const char *text,
                     size_t length);
void Line_DeleteText(Line *line, size_t position, size_t length);
size_t Line_Length(Line *line);
Slice Line_GetText(Line *line);

// Buffer operations
Buffer *Buffer_New(void);
void Buffer_Free(Buffer *buffer);
Line *Buffer_InsertLine(Buffer *buffer, size_t lineNumber);
void Buffer_DeleteLine(Buffer *buffer, size_t lineNumber);
Line *Buffer_GetLine(const Buffer *buffer, size_t lineNumber);
void Buffer_InsertChar(Buffer *buffer, size_t lineNumber, size_t column,
                       char c);
void Buffer_DeleteChar(Buffer *buffer, size_t lineNumber, size_t column);
void Buffer_SplitLine(Buffer *buffer, size_t lineNumber, size_t column);
void Buffer_JoinLine(Buffer *buffer, size_t lineNumber);
size_t Buffer_GetLineCount(const Buffer *buffer);
size_t Buffer_GetTotalBytes(const Buffer *buffer);
Slice Buffer_ToSlice(const Buffer *buffer);

// ============================================================================
// TERMINAL HANDLING
// ============================================================================

// Encapsulates terminal state
typedef struct Terminal {
  struct termios originalTermios;
  bool rawModeEnabled;
  bool termiosSaved;
} Terminal;

// Enables raw mode. Returns 0 on success, -1 on failure.
bool Terminal_EnableRawMode(Terminal *terminal);

// Disables raw mode. Returns 0 on success, -1 on failure.
bool Terminal_DisableRawMode(Terminal *terminal);

// Restores the terminal to its original state. Safe to call multiple times.
bool Terminal_Restore(Terminal *terminal);

// Clears the terminal screen using ANSI escape codes.
void Terminal_ClearScreen(const Terminal *terminal);

// Reads a single key from the terminal. Raw mode must be enabled. Returns the
// character read, or -1 on error.
int ReadKey(void);

// Handles signals for terminal cleanup. Intended for use as a signal handler.
void Terminal_HandleSignal(Terminal *terminal, int signalNumber);

// ============================================================================
// FILE I/O
// ============================================================================

// Read entire file into array (uses memory mapping for large files)
bool FileIO_Read(const char *path, Array *out);

// Write slice to file (atomic write on POSIX)
bool FileIO_Write(const char *path, Slice content);

// Memory-mapped file variant (zero-copy for large files)
typedef struct {
  Slice content;
  int fd; // File descriptor for cleanup
} MappedFile;

MappedFile FileIO_MMap(const char *path);
void FileIO_Unmap(MappedFile *file);

// ============================================================================
// EDITOR FEATURES
// ============================================================================

typedef struct {
  // Text buffer
  Buffer *buffer;

  // Cursor state
  size_t cursorX;
  size_t cursorY;
  size_t renderX;

  // Text row and column offsets
  size_t rowOffset;
  size_t columnOffset;

  // File state
  char *filename;
  bool isSaved;
} Tab;

void Tab_Init(Tab *tab);
void Tab_Free(Tab *tab);
void Tab_LoadFile(Tab *tab, const char *path);
void Tab_SaveFile(Tab *tab);
