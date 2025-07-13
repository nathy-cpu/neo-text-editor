#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <termios.h>

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
Slice Slice_From(const void* str);

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
void* Array_At(Array* array, size_t index);
size_t Array_Size(Array* array);
Slice Array_ToSlice(Array* array);

// Type-safe macros
#define Array_InitChar(arr, cap) Array_Init(arr, sizeof(char), cap, 64) // Cache line
#define Array_InitStruct(arr, type, cap) Array_Init(arr, sizeof(type), cap, alignof(type))
#define Array_Get(arr, type, idx) (*(type*)Array_At(arr, idx))
#define Array_AppendSlice(arr, slice) Array_Append(arr, slice.data, slice.size)

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
size_t GapBuffer_Size(GapBuffer* gapBuffer);
void GapBuffer_MoveGap(GapBuffer* gapBuffer, size_t newGapStart);

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
int Terminal_EnableRawMode(Terminal *terminal);

// Disables raw mode. Returns 0 on success, -1 on failure.
int Terminal_DisableRawMode(Terminal *terminal);

// Restores the terminal to its original state. Safe to call multiple times.
int Terminal_Restore(Terminal *terminal);

// Clears the terminal screen using ANSI escape codes.
void Terminal_ClearScreen(Terminal *terminal);

// Reads a single key from the terminal. Raw mode must be enabled. Returns the character read, or -1 on error.
int ReadKey(void);

// Handles signals for terminal cleanup. Intended for use as a signal handler.
void Terminal_HandleSignal(Terminal *terminal, int signalNumber);

// ============================================================================
// FILE I/O
// ============================================================================

// Read entire file into array (uses memory mapping for large files)
bool FileIO_Read(const char* path, Array* out);

// Write slice to file (atomic write on POSIX)
bool FileIO_Write(const char* path, Slice content);

// Memory-mapped file variant (zero-copy for large files)
typedef struct {
    Slice content;
    int fd; // File descriptor for cleanup
} MappedFile;

MappedFile FileIO_MMap(const char* path);
void FileIO_Unmap(MappedFile* file);

// ============================================================================
// EDITOR FEATURES
// ============================================================================

typedef struct {
    // Text buffer
    GapBuffer text;

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

void Tab_Init(Tab* tab);
void Tab_Free(Tab* tab);
void Tab_LoadFile(Tab* tab, const char* path);
void Tab_SaveFile(Tab* tab);
