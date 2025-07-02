#include "file_io.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <stdlib.h>

bool FileIO_Read(const char* path, Buffer* out) {
    int fd = open(path, O_RDONLY);
    if (fd == -1) return false;

    struct stat st;
    if (fstat(fd, &st)) {
        close(fd);
        return false;
    }

    // Use mmap for files > 1MB
    if (st.st_size > 1024 * 1024) {
        MappedFile mf = FileIO_MMap(path);
        if (mf.fd == -1) return false;
        
        bool ok = Buffer_Append(out, mf.content.data, mf.content.size);
        FileIO_Unmap(&mf);
        return ok;
    }

    // Small file fallback
    Buffer_Init(out, sizeof(char), st.st_size + 1, 0);
    ssize_t read_bytes = read(fd, out->data, st.st_size);
    close(fd);

    if (read_bytes != st.st_size) {
        Buffer_Free(out);
        return false;
    }

    out->size = read_bytes;
    return true;
}

bool FileIO_Write(const char* path, Slice content) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) return false;

    ssize_t written = write(fd, content.data, content.size);
    close(fd);

    return written == (ssize_t)content.size;
}

MappedFile FileIO_MMap(const char* path) {
    int fd = open(path, O_RDONLY);
    if (fd == -1) return (MappedFile){ .fd = -1 };

    struct stat st;
    if (fstat(fd, &st)) {
        close(fd);
        return (MappedFile){ .fd = -1 };
    }

    void* data = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (data == MAP_FAILED) {
        close(fd);
        return (MappedFile){ .fd = -1 };
    }

    return (MappedFile){
        .content = Slice_Make(data, st.st_size),
        .fd = fd
    };
}

void FileIO_Unmap(MappedFile* file) {
    if (file->fd != -1) {
        munmap((void*)file->content.data, file->content.size);
        close(file->fd);
        file->fd = -1;
    }
}