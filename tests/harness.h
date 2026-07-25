// Fork-isolated test harness.
//
// Each test runs in its own fork()ed child so a crash, sanitizer abort, or
// timeout in one test cannot take down the rest of the suite. Registration is
// automatic via __attribute__((constructor)); within a single translation
// unit constructors run in declaration order, so test order follows include
// order in tests/main.c.
//
// Red->green workflow for known bugs: land the regression test as
// TEST_XFAIL(suite, name, "<ticket>") -- the suite stays green while the bug
// exists (any abnormal child termination satisfies the expectation). Once the
// bug is fixed the test XPASSes, which FAILS the suite until the annotation
// is promoted to TEST() in the same commit as the fix.
#ifndef TEST_HARNESS_H
#define TEST_HARNESS_H

#include <stddef.h>

typedef enum {
    EXPECT_PASS, // child must exit(0)
    EXPECT_XFAIL, // known bug: any of FAIL/CRASH/TIMEOUT passes; exit(0) is XPASS (suite failure)
    EXPECT_SIGNAL, // child must die by exactly expectedSignal
    EXPECT_EXIT, // child must exit() with exactly expectedExit
} TestExpect;

typedef struct TestCase {
    const char* suite;
    const char* name;
    const char* bugReference; // ticket/issue label shown in output; NULL when not tied to a bug
    void (*function)(void);
    TestExpect expect;
    int expectedSignal; // EXPECT_SIGNAL only
    int expectedExit; // EXPECT_EXIT only
    unsigned timeoutSeconds; // 0 = default (10)
    const char* file;
    int line;
} TestCase;

void HarnessRegister(const TestCase* testCase);
int HarnessMain(int argc, char** argv);

#define TEST_HARNESS_DEFINE(suiteName, testName, expectKind, signalValue, exitValue, bugRef)     \
    static void TestBody_##suiteName##_##testName(void);                                         \
    __attribute__((constructor)) static void TestRegister_##suiteName##_##testName(void)         \
    {                                                                                            \
        static const TestCase testCase = {                                                       \
            .suite = #suiteName,                                                                 \
            .name = #testName,                                                                   \
            .bugReference = bugRef,                                                              \
            .function = TestBody_##suiteName##_##testName,                                       \
            .expect = expectKind,                                                                \
            .expectedSignal = signalValue,                                                       \
            .expectedExit = exitValue,                                                           \
            .timeoutSeconds = 0,                                                                 \
            .file = __FILE__,                                                                    \
            .line = __LINE__,                                                                    \
        };                                                                                       \
        HarnessRegister(&testCase);                                                              \
    }                                                                                            \
    static void TestBody_##suiteName##_##testName(void)

#define TEST(suiteName, testName) \
    TEST_HARNESS_DEFINE(suiteName, testName, EXPECT_PASS, 0, 0, NULL)

#define TEST_XFAIL(suiteName, testName, bugRef) \
    TEST_HARNESS_DEFINE(suiteName, testName, EXPECT_XFAIL, 0, 0, bugRef)

#define TEST_SIGNAL(suiteName, testName, signalNumber) \
    TEST_HARNESS_DEFINE(suiteName, testName, EXPECT_SIGNAL, signalNumber, 0, NULL)

#define TEST_EXITS(suiteName, testName, exitCode) \
    TEST_HARNESS_DEFINE(suiteName, testName, EXPECT_EXIT, 0, exitCode, NULL)

#endif // TEST_HARNESS_H
