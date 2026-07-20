#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "file_io.h"
#include "logger.h"
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

bool FileIoRead(const char* path, Array* out)
{
    int fileDescriptor = open(path, O_RDONLY);
    if (fileDescriptor == -1) {
        LOG_ERROR("Failed to open file for reading: %s", path);
        return false;
    }

    struct stat fileStat;
    if (fstat(fileDescriptor, &fileStat)) {
        LOG_ERROR("Failed to stat file: %s", path);
        close(fileDescriptor);
        return false;
    }

    LOG_INFO("Reading file: %s (%lld bytes)", path, (long long)fileStat.st_size);

    // Use mmap for files > 1MB
    if (fileStat.st_size > 1024 * 1024) {
        LOG_DEBUG("Using mmap for large file: %s", path);
        MappedFile mappedFile = FileIoMmap(path);
        if (mappedFile.fileDescriptor == -1)
            return false;

        // Initialize array with the correct size
        Array_Init(out, sizeof(char), mappedFile.content.size, 0);
        bool ok = Array_Append(out, mappedFile.content.data, mappedFile.content.size);
        MappedFile_Unmap(&mappedFile);
        return ok;
    }

    // Small file fallback
    Array_Init(out, sizeof(char), fileStat.st_size + 1, 0);
    ssize_t readBytes = read(fileDescriptor, out->data, fileStat.st_size);
    close(fileDescriptor);

    if (readBytes != fileStat.st_size) {
        LOG_ERROR(
            "Failed to read complete file: %s (read %zd of %lld bytes)", path, readBytes, (long long)fileStat.st_size);
        Array_Free(out);
        return false;
    }

    out->size = readBytes;
    return true;
}

bool FileIoWriteChunk(int fileDescriptor, const void* data, size_t len)
{
    const char* cursor = (const char*)data;
    size_t remaining = len;
    while (remaining > 0) {
        ssize_t written = write(fileDescriptor, cursor, remaining);
        if (written < 0) {
            if (errno == EINTR)
                continue;
            LOG_ERROR("FileIoWriteChunk: write() failed: %s", strerror(errno));
            return false;
        }
        cursor += written;
        remaining -= (size_t)written;
    }
    return true;
}

bool FileIoWrite(const char* path, Slice content)
{
    int fileDescriptor = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fileDescriptor == -1) {
        LOG_ERROR("Failed to open file for writing: %s", path);
        return false;
    }

    bool ok = FileIoWriteChunk(fileDescriptor, content.data, content.size);
    close(fileDescriptor);

    if (!ok) {
        LOG_ERROR("Failed to write entire content to file: %s", path);
        return false;
    }

    LOG_INFO("Successfully wrote file: %s (%zu bytes)", path, content.size);
    return true;
}

int FileIoOpenTempForAtomicWrite(const char* path, char* tempPathOut, size_t tempPathOutSize)
{
    char dirBuf[PATH_MAX];
    char baseBuf[PATH_MAX];
    if (strlen(path) >= sizeof(dirBuf)) {
        LOG_ERROR("FileIoOpenTempForAtomicWrite: path too long: %s", path);
        return -1;
    }
    strncpy(dirBuf, path, sizeof(dirBuf) - 1);
    dirBuf[sizeof(dirBuf) - 1] = '\0';
    strncpy(baseBuf, path, sizeof(baseBuf) - 1);
    baseBuf[sizeof(baseBuf) - 1] = '\0';
    const char* dir = dirname(dirBuf);
    const char* base = basename(baseBuf);

    int written = snprintf(tempPathOut, tempPathOutSize, "%s/.%s.neo-tmp-XXXXXX", dir, base);
    if (written < 0 || (size_t)written >= tempPathOutSize) {
        LOG_ERROR("FileIoOpenTempForAtomicWrite: temp path too long for: %s", path);
        return -1;
    }

    int fileDescriptor = mkstemp(tempPathOut);
    if (fileDescriptor == -1) {
        LOG_ERROR("FileIoOpenTempForAtomicWrite: mkstemp failed for '%s': %s", tempPathOut, strerror(errno));
        return -1;
    }

    // mkstemp creates the file with mode 0600; match FileIoWrite's conventional 0644.
    fchmod(fileDescriptor, 0644);
    return fileDescriptor;
}

void FileIoAbortAtomicWrite(int fileDescriptor, const char* tempPath)
{
    if (fileDescriptor != -1)
        close(fileDescriptor);
    unlink(tempPath);
}

bool FileIoCommitAtomicWrite(int fileDescriptor, const char* tempPath, const char* finalPath)
{
    if (fsync(fileDescriptor) != 0) {
        LOG_ERROR("FileIoCommitAtomicWrite: fsync failed for '%s': %s", tempPath, strerror(errno));
        close(fileDescriptor);
        unlink(tempPath);
        return false;
    }
    close(fileDescriptor);

    if (rename(tempPath, finalPath) != 0) {
        LOG_ERROR("FileIoCommitAtomicWrite: rename '%s' -> '%s' failed: %s", tempPath, finalPath, strerror(errno));
        unlink(tempPath);
        return false;
    }

    LOG_INFO("FileIoCommitAtomicWrite: atomically wrote '%s'", finalPath);
    return true;
}

MappedFile FileIoMmap(const char* path)
{
    int fileDescriptor = open(path, O_RDONLY);
    if (fileDescriptor == -1) {
        LOG_ERROR("mmap open failed for file: %s", path);
        return (MappedFile) { .fileDescriptor = -1 };
    }

    struct stat fileStat;
    if (fstat(fileDescriptor, &fileStat)) {
        close(fileDescriptor);
        return (MappedFile) { .fileDescriptor = -1 };
    }

    void* data = mmap(NULL, fileStat.st_size, PROT_READ, MAP_PRIVATE, fileDescriptor, 0);
    if (data == MAP_FAILED) {
        LOG_ERROR("mmap call failed for file: %s", path);
        close(fileDescriptor);
        return (MappedFile) { .fileDescriptor = -1 };
    }

    LOG_INFO("FileIoMmap: successfully memory-mapped file '%s' (%lld bytes)", path, (long long)fileStat.st_size);
    return (MappedFile) { .content = Slice_Make(data, fileStat.st_size), .fileDescriptor = fileDescriptor };
}

void MappedFile_Unmap(MappedFile* file)
{
    if (file->fileDescriptor != -1) {
        LOG_DEBUG(
            "MappedFile_Unmap: unmapping file descriptor %d (size=%zu)", file->fileDescriptor, file->content.size);
        munmap((void*)file->content.data, file->content.size);
        close(file->fileDescriptor);
        file->fileDescriptor = -1;
    }
}
