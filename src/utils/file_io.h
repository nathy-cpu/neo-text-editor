#pragma once

#include "array.h"
#include "slice.h"
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    Slice content;
    int fileDescriptor; // File descriptor for cleanup
} MappedFile;

/**
 * @brief Reads the entire contents of a file into a dynamic array.
 * Uses memory-mapping for files >1MB to optimize performance.
 */
bool FileIoRead(const char* path, Array* out);

/**
 * @brief Writes a full slice of data to a file, truncating any existing content.
 */
bool FileIoWrite(const char* path, Slice content);

/**
 * @brief Writes exactly `len` bytes to an open file descriptor.
 */
bool FileIoWriteChunk(int fileDescriptor, const void* data, size_t len);

/**
 * @brief Opens a new temp file in the same directory as `path` for atomic writes.
 */
int FileIoOpenTempForAtomicWrite(const char* path, char* tempPathOut, size_t tempPathOutSize);

/**
 * @brief Abandons an in-progress atomic write: closes the descriptor and removes temp file.
 */
void FileIoAbortAtomicWrite(int fileDescriptor, const char* tempPath);

/**
 * @brief Durably commits a completed atomic write.
 */
bool FileIoCommitAtomicWrite(int fileDescriptor, const char* tempPath, const char* finalPath, bool useFsync);

/**
 * @brief Memory-maps a file for zero-copy read access.
 */
MappedFile FileIoMmap(const char* path);

/**
 * @brief Unmaps a memory-mapped file and closes its descriptor.
 */
void MappedFile_Unmap(MappedFile* file);
