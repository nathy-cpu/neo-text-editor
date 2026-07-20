#include "../src/utils/logger.h"
#include "../src/utils/array.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static void test_log_basic(void)
{
    // Test 1: Init with UI enabled, file disabled
    Logger_Init("test_logger_file.log", LOG_LEVEL_INFO, false, true, 5);

    // Logging below threshold
    LOG_DEBUG("This is debug message, should be ignored");
    Array* messages = Logger_GetMessages();
    assert(messages != NULL);
    assert(Array_Size(messages) == 0);

    // Logging at/above threshold
    LOG_INFO("This is info message 1");
    LOG_WARN("This is warn message 2");
    assert(Array_Size(messages) == 2);

    char* m1 = *(char**)Array_At(messages, 0);
    char* m2 = *(char**)Array_At(messages, 1);
    assert(strstr(m1, "[INFO]") != NULL);
    assert(strstr(m1, "This is info message 1") != NULL);
    assert(strstr(m2, "[WARN]") != NULL);
    assert(strstr(m2, "This is warn message 2") != NULL);

    // Test limit: we have limit of 5. Let's add 4 more messages (total 6)
    LOG_INFO("msg 3");
    LOG_INFO("msg 4");
    LOG_INFO("msg 5");
    LOG_INFO("msg 6");

    assert(Array_Size(messages) == 5);
    // Oldest ("This is info message 1") should be evicted, so the first is now "This is warn message 2"
    m1 = *(char**)Array_At(messages, 0);
    assert(strstr(m1, "This is warn message 2") != NULL);
    char* m5 = *(char**)Array_At(messages, 4);
    assert(strstr(m5, "msg 6") != NULL);

    Logger_Free();
}

static void test_log_file(void)
{
    // Make sure we start clean
    remove("test_logger_file.log");

    // Init with File enabled, UI disabled
    Logger_Init("test_logger_file.log", LOG_LEVEL_WARN, true, false, 10);

    LOG_INFO("This should NOT go to file");
    LOG_WARN("This WARN should go to file");
    LOG_ERROR("This ERROR should also go to file");

    Logger_Free();

    // Verify file content
    FILE* f = fopen("test_logger_file.log", "r");
    assert(f != NULL);

    char line[512];
    int lineCount = 0;
    while (fgets(line, sizeof(line), f)) {
        lineCount++;
        if (lineCount == 1) {
            assert(strstr(line, "[WARN]") != NULL);
            assert(strstr(line, "This WARN should go to file") != NULL);
        } else if (lineCount == 2) {
            assert(strstr(line, "[ERROR]") != NULL);
            assert(strstr(line, "This ERROR should also go to file") != NULL);
        }
    }
    assert(lineCount == 2);
    fclose(f);

    remove("test_logger_file.log");
}

static void test_log_reconfigure(void)
{
    // Make sure we start clean
    remove("test_logger_file.log");

    // Test transition logic
    Logger_Init("test_logger_file.log", LOG_LEVEL_INFO, false, true, 10);
    LOG_INFO("Initial message");

    Array* messages = Logger_GetMessages();
    assert(Array_Size(messages) == 1);

    // Reconfigure to file only, change level to ERROR, max messages to 2
    Logger_Configure("test_logger_file.log", LOG_LEVEL_ERROR, true, false, 2);

    // UI should be cleared now
    assert(Logger_GetMessages() == NULL);

    // Logging to file
    LOG_INFO("Should not log info");
    LOG_ERROR("Should log error");

    Logger_Free();

    FILE* f = fopen("test_logger_file.log", "r");
    assert(f != NULL);
    char line[512];
    int lineCount = 0;
    while (fgets(line, sizeof(line), f)) {
        lineCount++;
        assert(strstr(line, "[ERROR]") != NULL);
        assert(strstr(line, "Should log error") != NULL);
    }
    assert(lineCount == 1);
    fclose(f);

    remove("test_logger_file.log");
}

static void test_log_parse_level_and_level_to_string(void)
{
    assert(Logger_ParseLevel("DEBUG", LOG_LEVEL_INFO) == LOG_LEVEL_DEBUG);
    assert(Logger_ParseLevel("info", LOG_LEVEL_DEBUG) == LOG_LEVEL_INFO); // case-insensitive
    assert(Logger_ParseLevel("WARN", LOG_LEVEL_INFO) == LOG_LEVEL_WARN);
    assert(Logger_ParseLevel("WARNING", LOG_LEVEL_INFO) == LOG_LEVEL_WARN); // alias
    assert(Logger_ParseLevel("Error", LOG_LEVEL_INFO) == LOG_LEVEL_ERROR);
    assert(Logger_ParseLevel("FATAL", LOG_LEVEL_INFO) == LOG_LEVEL_FATAL);
    assert(Logger_ParseLevel("not-a-level", LOG_LEVEL_WARN) == LOG_LEVEL_WARN); // falls back to default
    assert(Logger_ParseLevel(NULL, LOG_LEVEL_ERROR) == LOG_LEVEL_ERROR); // falls back to default

    assert(strcmp(Logger_LevelToString(LOG_LEVEL_DEBUG), "DEBUG") == 0);
    assert(strcmp(Logger_LevelToString(LOG_LEVEL_INFO), "INFO") == 0);
    assert(strcmp(Logger_LevelToString(LOG_LEVEL_WARN), "WARN") == 0);
    assert(strcmp(Logger_LevelToString(LOG_LEVEL_ERROR), "ERROR") == 0);
    assert(strcmp(Logger_LevelToString(LOG_LEVEL_FATAL), "FATAL") == 0);
    assert(strcmp(Logger_LevelToString((LogLevel)999), "UNKNOWN") == 0);
}

static void test_log_double_init_is_noop(void)
{
    Logger_Init("test_logger_file.log", LOG_LEVEL_DEBUG, false, true, 5);

    // A second Init call while already initialized must be ignored entirely --
    // if it took effect, the level would jump to FATAL and this DEBUG message
    // would be silently dropped instead of captured.
    Logger_Init("other.log", LOG_LEVEL_FATAL, true, false, 999);

    LOG_DEBUG("still at debug level");
    Array* messages = Logger_GetMessages();
    assert(messages != NULL && "UI logging should still be enabled from the first Init");
    assert(Array_Size(messages) == 1);
    assert(strstr(*(char**)Array_At(messages, 0), "still at debug level") != NULL);

    Logger_Free();
}

static void test_log_configure_from_uninitialized(void)
{
    // Logger_Configure on an uninitialized logger must delegate to Logger_Init
    // rather than touching global state that was never set up.
    Logger_Configure("test_logger_file.log", LOG_LEVEL_INFO, false, true, 5);

    LOG_INFO("configured from scratch");
    Array* messages = Logger_GetMessages();
    assert(messages != NULL);
    assert(Array_Size(messages) == 1);

    Logger_Free();
}

static void test_log_file_path_change(void)
{
    remove("log_a.log");
    remove("log_b.log");

    Logger_Init("log_a.log", LOG_LEVEL_INFO, true, false, 10);
    LOG_INFO("message in file A");

    // Reconfigure to a *different* file path while staying file-enabled -- must
    // close the old file and open the new one, not keep appending to A.
    Logger_Configure("log_b.log", LOG_LEVEL_INFO, true, false, 10);
    LOG_INFO("message in file B");

    Logger_Free();

    FILE* fa = fopen("log_a.log", "r");
    assert(fa != NULL);
    char line[512];
    int countA = 0;
    while (fgets(line, sizeof(line), fa)) {
        countA++;
        assert(strstr(line, "message in file A") != NULL);
    }
    assert(countA == 1);
    fclose(fa);

    FILE* fb = fopen("log_b.log", "r");
    assert(fb != NULL);
    int countB = 0;
    while (fgets(line, sizeof(line), fb)) {
        countB++;
        assert(strstr(line, "message in file B") != NULL);
    }
    assert(countB == 1);
    fclose(fb);

    remove("log_a.log");
    remove("log_b.log");
}

static void test_log_uninitialized_get_messages_and_before_init(void)
{
    // Relies on every other test leaving the logger freed at teardown.
    assert(Logger_GetMessages() == NULL);

    // Logging before Init (or after Free) must be a silent no-op, not a crash.
    LOG_INFO("should be dropped, logger not initialized");
    assert(Logger_GetMessages() == NULL);

    // Free on an already-uninitialized logger must also be a safe no-op.
    Logger_Free();
}

static void test_log_message_truncation(void)
{
    Logger_Init("test_logger_file.log", LOG_LEVEL_INFO, false, true, 5);

    char huge[2000];
    memset(huge, 'x', sizeof(huge) - 1);
    huge[sizeof(huge) - 1] = '\0';

    LOG_INFO("%s", huge); // format expands past msgBuf's 1024-byte limit; must not overflow/crash under ASan.

    Array* messages = Logger_GetMessages();
    assert(Array_Size(messages) == 1);
    char* captured = *(char**)Array_At(messages, 0);
    assert(strlen(captured) < 2048 && "Captured line must respect lineBuf's bound, not overrun it");
    assert(strstr(captured, "xxxx") != NULL && "Truncated message should still retain its (truncated) content");

    Logger_Free();
}
