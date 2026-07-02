#include "../neo.h"
#include <fcntl.h>
#include <stdlib.h>
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

bool FileIoWrite(const char* path, Slice content)
{
    int fileDescriptor = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fileDescriptor == -1) {
        LOG_ERROR("Failed to open file for writing: %s", path);
        return false;
    }

    ssize_t written = write(fileDescriptor, content.data, content.size);
    close(fileDescriptor);

    if (written != (ssize_t)content.size) {
        LOG_ERROR("Failed to write entire content to file: %s (wrote %zd of %zu bytes)", path, written, content.size);
        return false;
    }

    LOG_INFO("Successfully wrote file: %s (%zu bytes)", path, content.size);
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

    return (MappedFile) { .content = Slice_Make(data, fileStat.st_size), .fileDescriptor = fileDescriptor };
}

void MappedFile_Unmap(MappedFile* file)
{
    if (file->fileDescriptor != -1) {
        munmap((void*)file->content.data, file->content.size);
        close(file->fileDescriptor);
        file->fileDescriptor = -1;
    }
}
