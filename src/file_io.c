#include "file_io.h"
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

bool FileIO_Read(const char* path, Buffer* out_content)
{
    FILE* file = fopen(path, "rb");
    if (!file)
        return false;

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    Buffer_Init(out_content, file_size + 1); // +1 for null terminator if needed
    if (file_size > 0) {
        if (file_size > (long) out_content->capacity) {
            void* new_data = realloc(out_content->data, file_size + 1);
            if (!new_data) {
                fclose(file);
                return false;
            }
            out_content->data = new_data;
            out_content->capacity = file_size + 1;
        }

        size_t read = fread(out_content->data, 1, file_size, file);
        if ((long) read != file_size) {
            fclose(file);
            return false;
        }
        out_content->size = file_size;
    }
    fclose(file);
    return true;
}

bool FileIO_Write(const char* path, Slice content)
{
    FILE* file = fopen(path, "wb");
    if (!file)
        return false;
    size_t written = fwrite(content.data, 1, content.size, file);
    fclose(file);
    return written == content.size;
}

// --- Memory-Mapped File Implementation ---
MappedFile FileIO_MMap(const char* path)
{
    MappedFile result = { .fd = -1 };
    int fd = open(path, O_RDONLY);
    if (fd == -1)
        return result;

    struct stat st;
    if (fstat(fd, &st)) {
        close(fd);
        return result;
    }

    void* data = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (data == MAP_FAILED) {
        close(fd);
        return result;
    }

    result.content = Slice_Make(data, st.st_size);
    result.fd = fd;
    return result;
}

void FileIO_Unmap(MappedFile* file)
{
    if (file->fd != -1) {
        munmap((void*)file->content.data, file->content.size);
        close(file->fd);
        file->fd = -1;
    }
}
