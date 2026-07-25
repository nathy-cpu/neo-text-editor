#include "harness.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define HARNESS_MAX_TESTS 1024
#define HARNESS_DEFAULT_TIMEOUT_SECONDS 10

typedef enum {
    OUTCOME_PASSED, // exit(0)
    OUTCOME_FAILED_EXIT, // nonzero exit code (ASan reports land here)
    OUTCOME_CRASHED, // killed by a signal (bare assert() -> SIGABRT lands here)
    OUTCOME_TIMEOUT, // killed by SIGALRM
} TestOutcome;

static const TestCase* registeredTests[HARNESS_MAX_TESTS];
static size_t registeredTestCount = 0;

void HarnessRegister(const TestCase* testCase)
{
    if (registeredTestCount >= HARNESS_MAX_TESTS) {
        fprintf(stderr, "harness: too many tests (max %d)\n", HARNESS_MAX_TESTS);
        exit(1);
    }
    registeredTests[registeredTestCount++] = testCase;
}

static bool useColor = false;

static const char* ColorFor(const char* label)
{
    if (!useColor)
        return "";
    if (strcmp(label, "PASS") == 0 || strcmp(label, "XFAIL") == 0)
        return "\x1b[32m";
    if (strcmp(label, "SKIP") == 0)
        return "\x1b[33m";
    return "\x1b[31m"; // FAIL / CRASH / TIMEOUT / XPASS
}

static const char* ColorReset(void)
{
    return useColor ? "\x1b[0m" : "";
}

static void DumpCapturedOutput(FILE* capture)
{
    long size = ftell(capture);
    if (size <= 0)
        return;
    rewind(capture);
    char lineBuffer[4096];
    char lastCharacter = '\n';
    while (fgets(lineBuffer, sizeof lineBuffer, capture) != NULL) {
        fprintf(stdout, "    | %s", lineBuffer);
        size_t length = strlen(lineBuffer);
        if (length > 0)
            lastCharacter = lineBuffer[length - 1];
    }
    // Ensure the dump ends with a newline even if the child's last write had none.
    if (lastCharacter != '\n')
        fputc('\n', stdout);
}

static TestOutcome RunTestInChild(const TestCase* testCase, FILE* capture, int* detailOut)
{
    fflush(NULL);
    pid_t childPid = fork();
    if (childPid < 0) {
        fprintf(stderr, "harness: fork failed: %s\n", strerror(errno));
        exit(1);
    }
    if (childPid == 0) {
        dup2(fileno(capture), STDOUT_FILENO);
        dup2(fileno(capture), STDERR_FILENO);
        // Deterministic stdin regardless of how the suite was launched: when
        // run from an interactive terminal the child would otherwise inherit
        // the real TTY, and any code path that falls back to reading stdin
        // (e.g. Terminal_GetWindowSize's cursor-position query once ioctl
        // fails against the tmpfile stdout) blocks until the 10s alarm --
        // which showed up as dozens of spurious TIMEOUTs. /dev/null makes
        // such reads return EOF immediately, matching headless/CI behavior.
        // Tests that need scripted stdin dup2 their own pipe over it anyway.
        int devNull = open("/dev/null", O_RDONLY);
        if (devNull != -1) {
            dup2(devNull, STDIN_FILENO);
            close(devNull);
        }
        unsigned timeoutSeconds = testCase->timeoutSeconds ? testCase->timeoutSeconds
                                                           : HARNESS_DEFAULT_TIMEOUT_SECONDS;
        alarm(timeoutSeconds);
        testCase->function();
        // exit() (not _exit()) so atexit handlers -- including LeakSanitizer's
        // end-of-process check -- run per child, giving per-test leak detection.
        exit(0);
    }

    int status = 0;
    while (waitpid(childPid, &status, 0) < 0) {
        if (errno != EINTR) {
            fprintf(stderr, "harness: waitpid failed: %s\n", strerror(errno));
            exit(1);
        }
    }

    if (WIFEXITED(status)) {
        *detailOut = WEXITSTATUS(status);
        return *detailOut == 0 ? OUTCOME_PASSED : OUTCOME_FAILED_EXIT;
    }
    if (WIFSIGNALED(status)) {
        *detailOut = WTERMSIG(status);
        return *detailOut == SIGALRM ? OUTCOME_TIMEOUT : OUTCOME_CRASHED;
    }
    *detailOut = -1;
    return OUTCOME_CRASHED;
}

// Returns the result label; sets *isSuiteFailure.
static const char* ClassifyResult(const TestCase* testCase, TestOutcome outcome, int detail,
    bool* isSuiteFailure)
{
    *isSuiteFailure = false;
    switch (testCase->expect) {
    case EXPECT_PASS:
        if (outcome == OUTCOME_PASSED)
            return "PASS";
        *isSuiteFailure = true;
        return outcome == OUTCOME_TIMEOUT ? "TIMEOUT"
            : outcome == OUTCOME_CRASHED  ? "CRASH"
                                          : "FAIL";
    case EXPECT_XFAIL:
        if (outcome == OUTCOME_PASSED) {
            *isSuiteFailure = true;
            return "XPASS";
        }
        return "XFAIL";
    case EXPECT_SIGNAL:
        if (outcome == OUTCOME_CRASHED
            && (testCase->expectedSignal == 0 || detail == testCase->expectedSignal))
            return "PASS";
        *isSuiteFailure = true;
        return "FAIL";
    case EXPECT_EXIT:
        if ((outcome == OUTCOME_PASSED || outcome == OUTCOME_FAILED_EXIT)
            && detail == testCase->expectedExit)
            return "PASS";
        *isSuiteFailure = true;
        return "FAIL";
    }
    *isSuiteFailure = true;
    return "FAIL";
}

int HarnessMain(int argc, char** argv)
{
    const char* filter = NULL;
    bool verbose = false;
    bool listOnly = false;
    bool noFork = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--filter") == 0 && i + 1 < argc)
            filter = argv[++i];
        else if (strcmp(argv[i], "--list") == 0)
            listOnly = true;
        else if (strcmp(argv[i], "-v") == 0)
            verbose = true;
        else if (strcmp(argv[i], "--no-fork") == 0)
            noFork = true;
        else {
            fprintf(stderr,
                "usage: %s [--filter <substr>] [--list] [-v] [--no-fork]\n", argv[0]);
            return 2;
        }
    }

    useColor = isatty(STDOUT_FILENO);

    size_t passed = 0, failed = 0, crashed = 0, timedOut = 0;
    size_t expectedFailures = 0, unexpectedPasses = 0, ran = 0;

    for (size_t i = 0; i < registeredTestCount; i++) {
        const TestCase* testCase = registeredTests[i];
        char fullName[256];
        snprintf(fullName, sizeof fullName, "%s.%s", testCase->suite, testCase->name);
        if (filter != NULL && strstr(fullName, filter) == NULL)
            continue;

        if (listOnly) {
            printf("%s%s%s\n", fullName,
                testCase->bugReference ? "  " : "",
                testCase->bugReference ? testCase->bugReference : "");
            continue;
        }

        ran++;

        if (noFork) {
            // Debug mode: run in-process so gdb/rr can attach. Expectation and
            // timeout handling are disabled; a crash stops the suite.
            printf("[RUN ] %s\n", fullName);
            fflush(stdout);
            testCase->function();
            printf("[DONE] %s\n", fullName);
            passed++;
            continue;
        }

        FILE* capture = tmpfile();
        if (capture == NULL) {
            fprintf(stderr, "harness: tmpfile failed: %s\n", strerror(errno));
            return 1;
        }

        int detail = 0;
        TestOutcome outcome = RunTestInChild(testCase, capture, &detail);
        bool isSuiteFailure = false;
        const char* label = ClassifyResult(testCase, outcome, detail, &isSuiteFailure);

        if (strcmp(label, "PASS") == 0)
            passed++;
        else if (strcmp(label, "XFAIL") == 0)
            expectedFailures++;
        else if (strcmp(label, "XPASS") == 0)
            unexpectedPasses++;
        else if (strcmp(label, "TIMEOUT") == 0)
            timedOut++;
        else if (strcmp(label, "CRASH") == 0)
            crashed++;
        else
            failed++;

        printf("%s[%s]%s %s", ColorFor(label), label, ColorReset(), fullName);
        if (testCase->bugReference != NULL)
            printf("  (%s)", testCase->bugReference);
        if (strcmp(label, "XPASS") == 0)
            printf("  -- promote to TEST()");
        if (strcmp(label, "CRASH") == 0)
            printf("  -- killed by signal %d", detail);
        if (strcmp(label, "FAIL") == 0 && outcome == OUTCOME_FAILED_EXIT)
            printf("  -- exit code %d", detail);
        printf("\n");

        if (isSuiteFailure || verbose)
            DumpCapturedOutput(capture);
        fclose(capture);
    }

    if (listOnly)
        return 0;

    size_t suiteFailures = failed + crashed + timedOut + unexpectedPasses;
    printf("\n%zu ran: %zu passed, %zu failed, %zu crashed, %zu timeout, "
           "%zu xfail, %zu xpass -- %s%s%s\n",
        ran, passed, failed, crashed, timedOut, expectedFailures, unexpectedPasses,
        ColorFor(suiteFailures ? "FAIL" : "PASS"), suiteFailures ? "FAIL" : "OK",
        ColorReset());
    return suiteFailures ? 1 : 0;
}
