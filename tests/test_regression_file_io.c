// Regression tests for file-I/O behavior: the atomic save path's handling
// of permissions and symlinks, and descriptor hygiene on large reads. The
// permission tests double as coverage for the fchmod return-value check.
//
// This file is #included into tests/main.c (single translation unit); all
// helpers are static and prefixed with RegressionFileIo to avoid collisions.
//
// Cleanup discipline: because a failing assert aborts the test on the spot,
// each test gathers its observations first, removes its scratch files, and
// only then asserts -- so fixtures don't leak on a failure path.

#include "../src/core/buffer.h"
#include "../src/utils/array.h"
#include "../src/utils/file_io.h"
#include "../src/utils/slice.h"
#include <assert.h>
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// ============================================================================
// Helpers
// ============================================================================

// Builds an in-memory buffer holding `text`, for driving the atomic-save
// path (Buffer_WriteToFileStreaming).
static Buffer* RegressionFileIoCreateBufferWithText(const char* text)
{
    Buffer* buffer = Buffer_New();
    assert(buffer != NULL);
    Buffer_InsertText(buffer, 0, text, strlen(text));
    return buffer;
}

// Counts this process's open file descriptors by listing /proc/self/fd.
// The directory stream's own descriptor inflates both measurements equally,
// so before/after comparisons remain exact.
static size_t RegressionFileIoCountOpenFileDescriptors(void)
{
    DIR* directory = opendir("/proc/self/fd");
    assert(directory != NULL);
    size_t count = 0;
    struct dirent* entry;
    while ((entry = readdir(directory)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        count++;
    }
    closedir(directory);
    return count;
}

// ============================================================================
// The atomic save path (mkstemp + fchmod + rename) must preserve the
// target's permissions and must write through symlinks rather than
// replacing them with regular files.
// ============================================================================

TEST(regression_fileio, atomic_write_preserves_permissions)
{
    char directoryPath[] = "/tmp/neo_test_perm_XXXXXX";
    assert(mkdtemp(directoryPath) != NULL);
    char filePath[PATH_MAX];
    snprintf(filePath, sizeof(filePath), "%s/target.txt", directoryPath);

    const char* newContent = "new content\n";

    assert(FileIoWrite(filePath, Slice_From("old content\n")));
    assert(chmod(filePath, 0640) == 0);

    Buffer* buffer = RegressionFileIoCreateBufferWithText(newContent);
    bool writeOk = Buffer_WriteToFileStreaming(buffer, filePath, false);
    Buffer_Free(buffer);
    assert(writeOk);

    struct stat fileStat;
    assert(stat(filePath, &fileStat) == 0);
    mode_t savedMode = fileStat.st_mode & 07777;

    Array fileContent = { 0 };
    bool readOk = FileIoRead(filePath, &fileContent);

    unlink(filePath);
    rmdir(directoryPath);

    assert(readOk);
    assert(fileContent.size == strlen(newContent));
    assert(memcmp(fileContent.data, newContent, fileContent.size) == 0);
    Array_Free(&fileContent);

    // The save must preserve the file's existing mode; an unconditional
    // fchmod(0644) on the temp file would clobber it.
    assert(savedMode == 0640);
}

TEST(regression_fileio, atomic_write_follows_symlink)
{
    char directoryPath[] = "/tmp/neo_test_link_XXXXXX";
    assert(mkdtemp(directoryPath) != NULL);
    char targetPath[PATH_MAX];
    char linkPath[PATH_MAX];
    snprintf(targetPath, sizeof(targetPath), "%s/target.txt", directoryPath);
    snprintf(linkPath, sizeof(linkPath), "%s/link.txt", directoryPath);

    const char* newContent = "updated via symlink\n";

    assert(FileIoWrite(targetPath, Slice_From("old content\n")));
    assert(symlink("target.txt", linkPath) == 0);

    Buffer* buffer = RegressionFileIoCreateBufferWithText(newContent);
    bool writeOk = Buffer_WriteToFileStreaming(buffer, linkPath, false);
    Buffer_Free(buffer);

    struct stat linkStat;
    bool linkStatOk = (lstat(linkPath, &linkStat) == 0);
    bool linkStillSymlink = linkStatOk && S_ISLNK(linkStat.st_mode);

    struct stat targetStat;
    bool targetIsRegular = (stat(targetPath, &targetStat) == 0) && S_ISREG(targetStat.st_mode);

    Array targetContent = { 0 };
    bool targetReadOk = FileIoRead(targetPath, &targetContent);

    unlink(linkPath);
    unlink(targetPath);
    rmdir(directoryPath);

    assert(writeOk);
    // Saving through a symlink must write the TARGET and leave the link in
    // place; the rename() must not replace the link itself with a regular
    // file while the target keeps its old content.
    assert(linkStillSymlink);
    assert(targetIsRegular);
    assert(targetReadOk);
    assert(targetContent.size == strlen(newContent));
    assert(memcmp(targetContent.data, newContent, targetContent.size) == 0);
    Array_Free(&targetContent);
}

TEST(regression_fileio, atomic_write_dangling_symlink)
{
    char directoryPath[] = "/tmp/neo_test_dangle_XXXXXX";
    assert(mkdtemp(directoryPath) != NULL);
    char missingTargetPath[PATH_MAX];
    char linkPath[PATH_MAX];
    snprintf(missingTargetPath, sizeof(missingTargetPath), "%s/missing.txt", directoryPath);
    snprintf(linkPath, sizeof(linkPath), "%s/link.txt", directoryPath);

    const char* newContent = "created through dangling link\n";

    // Symlink to a target that does not exist yet.
    assert(symlink("missing.txt", linkPath) == 0);

    Buffer* buffer = RegressionFileIoCreateBufferWithText(newContent);
    bool writeOk = Buffer_WriteToFileStreaming(buffer, linkPath, false);
    Buffer_Free(buffer);

    struct stat linkStat;
    bool linkStillSymlink = (lstat(linkPath, &linkStat) == 0) && S_ISLNK(linkStat.st_mode);

    struct stat targetStat;
    bool targetCreated = (stat(missingTargetPath, &targetStat) == 0) && S_ISREG(targetStat.st_mode);

    Array targetContent = { 0 };
    bool targetReadOk = FileIoRead(missingTargetPath, &targetContent);

    unlink(linkPath);
    unlink(missingTargetPath);
    rmdir(directoryPath);

    assert(writeOk);
    // Saving through a dangling symlink must create the target with the
    // content and keep the link a symlink; the rename() must not replace
    // the link itself and leave the target uncreated.
    assert(linkStillSymlink);
    assert(targetCreated);
    assert(targetReadOk);
    assert(targetContent.size == strlen(newContent));
    assert(memcmp(targetContent.data, newContent, targetContent.size) == 0);
    Array_Free(&targetContent);
}

// Saving to a brand-new path yields a regular file with conventional 0644
// permissions (independent of umask, since the mode is set with fchmod).
TEST(regression_fileio, atomic_write_new_file_default_mode)
{
    char directoryPath[] = "/tmp/neo_test_newfile_XXXXXX";
    assert(mkdtemp(directoryPath) != NULL);
    char filePath[PATH_MAX];
    snprintf(filePath, sizeof(filePath), "%s/new.txt", directoryPath);

    const char* newContent = "fresh file\n";

    Buffer* buffer = RegressionFileIoCreateBufferWithText(newContent);
    bool writeOk = Buffer_WriteToFileStreaming(buffer, filePath, false);
    Buffer_Free(buffer);

    struct stat fileStat;
    bool fileExists = (stat(filePath, &fileStat) == 0);
    mode_t savedMode = fileExists ? (fileStat.st_mode & 07777) : 0;
    bool isRegular = fileExists && S_ISREG(fileStat.st_mode);

    Array fileContent = { 0 };
    bool readOk = FileIoRead(filePath, &fileContent);

    unlink(filePath);
    rmdir(directoryPath);

    assert(writeOk);
    assert(fileExists);
    assert(isRegular);
    assert(savedMode == 0644);
    assert(readOk);
    assert(fileContent.size == strlen(newContent));
    assert(memcmp(fileContent.data, newContent, fileContent.size) == 0);
    Array_Free(&fileContent);
}

// ============================================================================
// FileIoRead's mmap path (files > 1MB) must close the file descriptor from
// its initial open(): FileIoMmap opens the file a second time and cleans up
// only that descriptor, so leaving the first one open would leak one fd per
// large read.
// ============================================================================

TEST(regression_fileio, fileio_read_large_no_fd_leak)
{
    char directoryPath[] = "/tmp/neo_test_leak_XXXXXX";
    assert(mkdtemp(directoryPath) != NULL);
    char filePath[PATH_MAX];
    snprintf(filePath, sizeof(filePath), "%s/large.bin", directoryPath);

    // 2MB, comfortably above FileIoRead's 1MB mmap threshold.
    const size_t largeSize = 2 * 1024 * 1024;
    char* largeContent = malloc(largeSize);
    assert(largeContent != NULL);
    memset(largeContent, 'x', largeSize);
    assert(FileIoWrite(filePath, Slice_Make(largeContent, largeSize)));
    free(largeContent);

    size_t openBefore = RegressionFileIoCountOpenFileDescriptors();

    Array fileContent = { 0 };
    bool readOk = FileIoRead(filePath, &fileContent);
    size_t readSize = fileContent.size;
    if (readOk) {
        // FileIoRead hands ownership of the array's storage to the caller.
        Array_Free(&fileContent);
    }

    size_t openAfter = RegressionFileIoCountOpenFileDescriptors();

    unlink(filePath);
    rmdir(directoryPath);

    assert(readOk);
    assert(readSize == largeSize);
    // A completed large read must leave no descriptor behind.
    assert(openAfter == openBefore);
}
