#include "../neo.h"
#include <fcntl.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

bool FileIORead(const char* path, Array* out)
{
    int fd = open(path, O_RDONLY);
    if (fd == -1)
        return false;

    struct stat st;
    if (fstat(fd, &st)) {
        close(fd);
        return false;
    }

    // Use mmap for files > 1MB
    if (st.st_size > 1024 * 1024) {
        MappedFile mf = FileIOMMap(path);
        if (mf.fd == -1)
            return false;

        // Initialize array with the correct size
        Array_Init(out, sizeof(char), mf.content.size, 0);
        bool ok = Array_Append(out, mf.content.data, mf.content.size);
        MappedFile_Unmap(&mf);
        return ok;
    }

    // Small file fallback
    Array_Init(out, sizeof(char), st.st_size + 1, 0);
    ssize_t read_bytes = read(fd, out->data, st.st_size);
    close(fd);

    if (read_bytes != st.st_size) {
        Array_Free(out);
        return false;
    }

    out->size = read_bytes;
    return true;
}

bool FileIOWrite(const char* path, Slice content)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd == -1)
        return false;

    ssize_t written = write(fd, content.data, content.size);
    close(fd);

    return written == (ssize_t)content.size;
}

MappedFile FileIOMMap(const char* path)
{
    int fd = open(path, O_RDONLY);
    if (fd == -1)
        return (MappedFile) { .fd = -1 };

    struct stat st;
    if (fstat(fd, &st)) {
        close(fd);
        return (MappedFile) { .fd = -1 };
    }

    void* data = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (data == MAP_FAILED) {
        close(fd);
        return (MappedFile) { .fd = -1 };
    }

    return (MappedFile) { .content = Slice_Make(data, st.st_size), .fd = fd };
}

void MappedFile_Unmap(MappedFile* file)
{
    if (file->fd != -1) {
        munmap((void*)file->content.data, file->content.size);
        close(file->fd);
        file->fd = -1;
    }
}
