#include "../neo.h"
#include <fcntl.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

bool FileIoRead(const char* path, Array* out)
{
    int fileDescriptor = open(path, O_RDONLY);
    if (fileDescriptor == -1)
        return false;

    struct stat fileStat;
    if (fstat(fileDescriptor, &fileStat)) {
        close(fileDescriptor);
        return false;
    }

    // Use mmap for files > 1MB
    if (fileStat.st_size > 1024 * 1024) {
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
        Array_Free(out);
        return false;
    }

    out->size = readBytes;
    return true;
}

bool FileIoWrite(const char* path, Slice content)
{
    int fileDescriptor = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fileDescriptor == -1)
        return false;

    ssize_t written = write(fileDescriptor, content.data, content.size);
    close(fileDescriptor);

    return written == (ssize_t)content.size;
}

MappedFile FileIoMmap(const char* path)
{
    int fileDescriptor = open(path, O_RDONLY);
    if (fileDescriptor == -1)
        return (MappedFile) { .fileDescriptor = -1 };

    struct stat fileStat;
    if (fstat(fileDescriptor, &fileStat)) {
        close(fileDescriptor);
        return (MappedFile) { .fileDescriptor = -1 };
    }

    void* data = mmap(NULL, fileStat.st_size, PROT_READ, MAP_PRIVATE, fileDescriptor, 0);
    if (data == MAP_FAILED) {
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
