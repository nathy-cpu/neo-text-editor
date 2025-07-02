#pragma once
#include "buffer.h"
#include "slice.h"

// Read entire file into buffer (uses memory mapping for large files)
bool FileIO_Read(const char* path, Buffer* out);

// Write slice to file (atomic write on POSIX)
bool FileIO_Write(const char* path, Slice content);

// Memory-mapped file variant (zero-copy for large files)
typedef struct {
    Slice content;
    int fd; // File descriptor for cleanup
} MappedFile;

MappedFile FileIO_MMap(const char* path);
void FileIO_Unmap(MappedFile* file);