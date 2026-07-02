#include "../src/neo.h"
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
