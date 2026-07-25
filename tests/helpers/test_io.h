// Shared I/O test helpers (single-TU test build: include from test files).
#ifndef TEST_HELPERS_TEST_IO_H
#define TEST_HELPERS_TEST_IO_H

#include "../../src/terminal/terminal.h"
#include <assert.h>
#include <stddef.h>
#include <unistd.h>

// Redirects STDIN_FILENO to a pipe pre-loaded with `bytes`, calls ReadKey(),
// then restores the original stdin. `bytes` must represent one complete key
// sequence understood by ReadKey -- once it's fully resolved, ReadKey returns
// without attempting to read further, so closing the pipe's write end (giving
// EOF for anything beyond what we supplied) cannot cause it to block.
static inline int ReadKeyFromBytes(const char* bytes, size_t len)
{
    int pipefd[2];
    assert(pipe(pipefd) == 0);
    ssize_t written = write(pipefd[1], bytes, len);
    assert(written == (ssize_t)len);
    close(pipefd[1]);

    int savedStdin = dup(STDIN_FILENO);
    assert(savedStdin != -1);
    assert(dup2(pipefd[0], STDIN_FILENO) != -1);
    close(pipefd[0]);

    int key = ReadKey();

    assert(dup2(savedStdin, STDIN_FILENO) != -1);
    close(savedStdin);
    return key;
}

#endif // TEST_HELPERS_TEST_IO_H
