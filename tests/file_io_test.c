#include "../src/file_io.h"
#include "../src/buffer.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// Helper: Create a temporary test file
static void create_test_file(const char *path, const char *content) {
    FILE *file = fopen(path, "w");
    assert(file && "Failed to create test file");
    fputs(content, file);
    fclose(file);
}

// Test FileIO_Read and FileIO_Write
static void test_read_write() {
    const char *path = "testfile.txt";
    const char *content = "Hello, world!";
    create_test_file(path, content);

    // Test FileIO_Read
    Buffer buf1 = {0};
    assert(FileIO_Read(path, &buf1) && "Read failed");
    assert(buf1.size == strlen(content) && "Incorrect read length");
    assert(memcmp(buf1.data, content, buf1.size) == 0 && "Read content mismatch");

    // Test FileIO_Write
    const char *new_content = "Goodbye!";
    Slice new_slice = Slice_Make(new_content, strlen(new_content));
    assert(FileIO_Write(path, new_slice) && "Write failed");

    // Verify the write
    Buffer buf2 = {0};
    assert(FileIO_Read(path, &buf2) && "Re-read failed");
    assert(buf2.size == strlen(new_content) && "Incorrect write length");
    assert(memcmp(buf2.data, new_content, buf2.size) == 0 && "Write content mismatch");

    Buffer_Free(&buf1);
    Buffer_Free(&buf2);
    unlink(path); // Delete the test file
}

// Test FileIO_MMap
static void test_mmap() {
    const char *path = "mmap_test.txt";
    const char *content = "Memory-mapped file test";
    create_test_file(path, content);

    MappedFile file = FileIO_MMap(path);
    assert(file.fd != -1 && "MMap failed");
    assert(file.content.size == strlen(content) && "MMap length mismatch");
    assert(memcmp(file.content.data, content, file.content.size) == 0 && "MMap content mismatch");

    FileIO_Unmap(&file);
    unlink(path);
}

// Test error cases
static void test_errors() {
    // Nonexistent file
    Buffer buffer = {0};
    assert(!FileIO_Read("nonexistent.txt", &buffer) && "Read should fail");

    // Permission denied (create a file and make it unreadable)
    const char *path = "no_perms.txt";
    create_test_file(path, "test");
    chmod(path, 0000); // Remove all permissions

    assert(!FileIO_Read(path, &buffer) && "Read should fail (permissions)");
    chmod(path, 0644); // Restore permissions
    unlink(path);
}

static void test_null_safety() {
    // Test Buffer_Append with NULL
    Buffer buf = {0};
    Buffer_InitChar(&buf, 10);
    assert(!Buffer_Append(&buf, NULL, 10) && "Should reject NULL source");
    Buffer_Free(&buf);

    // Test empty buffer
    Buffer buf2 = {0};
    assert(Buffer_Append(&buf2, "hello", 0) && "Empty append should succeed");
    Buffer_Free(&buf2);
}

static void test_large_file() {
    const char *path = "largefile.bin";
    const size_t size = 1024 * 1024 * 1024; // 1GB

    // Create a 1GB file (manual test)
    printf("Creating 1GB test file...\n");
    FILE *file = fopen(path, "wb");
    assert(file && "Failed to create large file");
    fseek(file, size - 1, SEEK_SET);
    fputc('\0', file);
    fclose(file);

    // Test reading
    Buffer buf = {0};
    assert(FileIO_Read(path, &buf) && "Large read failed");
    assert(buf.size == size && "Incorrect large file length");
    Buffer_Free(&buf);

    unlink(path);
}

int main() {
    test_read_write();
    test_mmap();
    test_errors();
    test_null_safety();
    test_large_file();
    printf("All file_io tests passed!\n");
    return 0;
}
