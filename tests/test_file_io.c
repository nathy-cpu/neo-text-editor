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

static void test_mmap_threshold_boundary() {
    // FileIoRead switches from the small-file (read()) path to the mmap path
    // strictly above 1MB; verify both sides of that exact boundary.
    const size_t oneMb = 1024 * 1024;

    const char *atThreshold = "at_threshold.bin";
    FILE *f1 = fopen(atThreshold, "wb");
    assert(f1 && "Failed to create at-threshold file");
    fputs("HEAD", f1);
    fseek(f1, (long)oneMb - 1, SEEK_SET);
    fputc('\0', f1);
    fclose(f1);

    Array arr1 = {0};
    assert(FileIoRead(atThreshold, &arr1) && "Read at exactly 1MB should use the small-file path");
    assert(arr1.size == oneMb);
    assert(memcmp(arr1.data, "HEAD", 4) == 0);
    Array_Free(&arr1);
    unlink(atThreshold);

    const char *overThreshold = "over_threshold.bin";
    FILE *f2 = fopen(overThreshold, "wb");
    assert(f2 && "Failed to create over-threshold file");
    fputs("HEAD", f2);
    fseek(f2, (long)oneMb, SEEK_SET); // one byte over 1MB
    fputc('\0', f2);
    fclose(f2);

    Array arr2 = {0};
    assert(FileIoRead(overThreshold, &arr2) && "Read at 1MB+1 should use the mmap path");
    assert(arr2.size == oneMb + 1);
    assert(memcmp(arr2.data, "HEAD", 4) == 0);
    Array_Free(&arr2);
    unlink(overThreshold);
}

static void test_zero_byte_file() {
    const char *path = "empty.txt";
    CreateTestFile(path, "");

    Array arr = {0};
    assert(FileIoRead(path, &arr) && "Reading a zero-byte file should succeed");
    assert(arr.size == 0);
    Array_Free(&arr);

    Slice empty = Slice_Make(NULL, 0);
    assert(FileIoWrite(path, empty) && "Writing zero bytes should succeed");

    struct stat st;
    assert(stat(path, &st) == 0);
    assert(st.st_size == 0);

    unlink(path);
}

static void test_write_failure() {
    // The parent directory doesn't exist, so open(..., O_CREAT) must fail.
    Slice content = Slice_From("data");
    assert(!FileIoWrite("no_such_dir/file.txt", content) && "Write to a nonexistent directory should fail");
}

static void test_mmap_open_failure() {
    MappedFile file = FileIoMmap("nonexistent_for_mmap.txt");
    assert(file.fileDescriptor == -1 && "Mmap of a nonexistent file should fail cleanly");
}

static void test_mapped_file_unmap_safety() {
    // Unmapping a MappedFile that was never successfully mapped is a no-op.
    MappedFile neverMapped = { .fileDescriptor = -1 };
    MappedFile_Unmap(&neverMapped);
    assert(neverMapped.fileDescriptor == -1);

    // Unmapping twice must not double-close/double-munmap.
    const char *path = "double_unmap.txt";
    CreateTestFile(path, "data");
    MappedFile file = FileIoMmap(path);
    assert(file.fileDescriptor != -1);

    MappedFile_Unmap(&file);
    assert(file.fileDescriptor == -1);
    MappedFile_Unmap(&file); // must be a safe no-op
    assert(file.fileDescriptor == -1);

    unlink(path);
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
