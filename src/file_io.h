#pragma once
#include "buffer.h"
#include "slice.h"

// Read an entire file into a Buffer (supports large files via mmap or
// streaming)
bool FileIO_Read(const char* path, Buffer* out_content);

// Write a Slice (or Buffer) to a file
bool FileIO_Write(const char* path, Slice content);

// Memory-mapped file (alternative for very large files)
typedef struct {
    Slice content; // Points to mmap'd data
    int fd; // File descriptor (for munmap)
} MappedFile;

MappedFile FileIO_MMap(const char* path);
void FileIO_Unmap(MappedFile* file);
