#include "../src/neo.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/**
 * @brief Helper function to create a temporary test file with contents.
 * @param path Path to the file.
 * @param content The text content to write.
 */
static void CreateTestFile(const char *path, const char *content) {
    FILE *file = fopen(path, "w");
    assert(file && "Failed to create test file");
    fputs(content, file);
    fclose(file);
}

// Test FileIORead and FileIOWrite
static void test_read_write() {
    const char *path = "testfile.txt";
    const char *content = "Hello, world!";
    CreateTestFile(path, content);

    // Test FileIoRead
    Array arr1 = {0};
    assert(FileIoRead(path, &arr1) && "Read failed");
    assert(arr1.size == strlen(content) && "Incorrect read length");
    assert(memcmp(arr1.data, content, arr1.size) == 0 && "Read content mismatch");

    // Test FileIoWrite
    const char *new_content = "Goodbye!";
    Slice new_slice = Slice_From(new_content);
    assert(FileIoWrite(path, new_slice) && "Write failed");

    // Verify the write
    Array arr2 = {0};
    assert(FileIoRead(path, &arr2) && "Re-read failed");
    assert(arr2.size == strlen(new_content) && "Incorrect write length");
    assert(memcmp(arr2.data, new_content, arr2.size) == 0 && "Write content mismatch");

    Array_Free(&arr1);
    Array_Free(&arr2);
    unlink(path); // Delete the test file
}

// Test FileIOMMap
static void test_mmap() {
    const char *path = "mmap_test.txt";
    const char *content = "Memory-mapped file test";
    CreateTestFile(path, content);

    MappedFile file = FileIoMmap(path);
    assert(file.fileDescriptor != -1 && "MMap failed");
    assert(file.content.size == strlen(content) && "MMap length mismatch");
    assert(memcmp(file.content.data, content, file.content.size) == 0 && "MMap content mismatch");

    MappedFile_Unmap(&file);
    unlink(path);
}

// Test error cases
static void test_errors() {
    // Nonexistent file
    Array array = {0};
    assert(!FileIoRead("nonexistent.txt", &array) && "Read should fail");

    // Permission denied (create a file and make it unreadable)
    const char *path = "no_perms.txt";
    CreateTestFile(path, "test");
    chmod(path, 0000); // Remove all permissions

    assert(!FileIoRead(path, &array) && "Read should fail (permissions)");
    chmod(path, 0644); // Restore permissions
    unlink(path);
}

static void test_null_safety() {
    // Test Array_Append with NULL
    Array arr = {0};
    Array_InitChar(&arr, 10);
    assert(!Array_Append(&arr, NULL, 10) && "Should reject NULL source");
    Array_Free(&arr);

    // Test empty array
    Array arr2 = {0};
    assert(Array_Append(&arr2, "hello", 0) && "Empty append should succeed");
    Array_Free(&arr2);
}

static void test_large_file() {
    const char *path = "largefile.bin";
    const size_t size = 1024 * 1024 * 1024; // 1GB

    // Create a 1GB file (manual test)
    printf("Creating 1GB test file...\n\r");
    FILE *file = fopen(path, "wb");
    assert(file && "Failed to create large file");
    fseek(file, size - 1, SEEK_SET);
    fputc('\0', file);
    fclose(file);

    // Test reading
    Array arr = {0};
    assert(FileIoRead(path, &arr) && "Large read failed");
    assert(arr.size == size && "Incorrect large file length");
    Array_Free(&arr);

    unlink(path);
}
