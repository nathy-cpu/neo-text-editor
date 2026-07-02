#define _POSIX_C_SOURCE 200809L
#include "../neo.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static char* g_logFilePath = NULL;
static LogLevel g_logLevel = LOG_LEVEL_INFO;
static bool g_logToFile = false;
static bool g_logToUi = false;
static int g_logMaxMessages = 1000;
static Array g_logMessages;
static bool g_loggerInitialized = false;
static FILE* g_logFile = NULL;

const char* Logger_LevelToString(LogLevel level)
{
    switch (level) {
    case LOG_LEVEL_DEBUG:
        return "DEBUG";
    case LOG_LEVEL_INFO:
        return "INFO";
    case LOG_LEVEL_WARN:
        return "WARN";
    case LOG_LEVEL_ERROR:
        return "ERROR";
    case LOG_LEVEL_FATAL:
        return "FATAL";
    default:
        return "UNKNOWN";
    }
}

LogLevel Logger_ParseLevel(const char* levelStr, LogLevel defaultLevel)
{
    if (!levelStr)
        return defaultLevel;
    if (strcasecmp(levelStr, "DEBUG") == 0)
        return LOG_LEVEL_DEBUG;
    if (strcasecmp(levelStr, "INFO") == 0)
        return LOG_LEVEL_INFO;
    if (strcasecmp(levelStr, "WARN") == 0 || strcasecmp(levelStr, "WARNING") == 0)
        return LOG_LEVEL_WARN;
    if (strcasecmp(levelStr, "ERROR") == 0)
        return LOG_LEVEL_ERROR;
    if (strcasecmp(levelStr, "FATAL") == 0)
        return LOG_LEVEL_FATAL;
    return defaultLevel;
}

void Logger_Init(const char* logFile, LogLevel level, bool logToFile, bool logToUi, int maxMessages)
{
    if (g_loggerInitialized)
        return;

    g_logLevel = level;
    g_logToFile = logToFile;
    g_logToUi = logToUi;
    g_logMaxMessages = (maxMessages > 0) ? maxMessages : 1000;

    if (g_logToFile && logFile) {
        g_logFilePath = strdup(logFile);
        g_logFile = fopen(g_logFilePath, "a");
    }

    if (g_logToUi) {
        Array_Init(&g_logMessages, sizeof(char*), 16, alignof(void*));
    }

    g_loggerInitialized = true;
}

void Logger_Configure(const char* logFile, LogLevel level, bool logToFile, bool logToUi, int maxMessages)
{
    if (!g_loggerInitialized) {
        Logger_Init(logFile, level, logToFile, logToUi, maxMessages);
        return;
    }

    g_logLevel = level;
    g_logMaxMessages = (maxMessages > 0) ? maxMessages : 1000;

    // Handle File logging transition
    if (logToFile && logFile) {
        if (!g_logToFile || !g_logFilePath || strcmp(g_logFilePath, logFile) != 0) {
            if (g_logFile) {
                fclose(g_logFile);
                g_logFile = NULL;
            }
            free(g_logFilePath);
            g_logFilePath = strdup(logFile);
            g_logFile = fopen(g_logFilePath, "a");
        }
        g_logToFile = true;
    } else {
        if (g_logFile) {
            fclose(g_logFile);
            g_logFile = NULL;
        }
        g_logToFile = false;
    }

    // Handle UI logging transition
    if (logToUi) {
        if (!g_logToUi) {
            Array_Init(&g_logMessages, sizeof(char*), 16, alignof(void*));
        }
        g_logToUi = true;
    } else {
        if (g_logToUi) {
            for (size_t i = 0; i < Array_Size(&g_logMessages); i++) {
                char* msg = Array_Get(&g_logMessages, char*, i);
                free(msg);
            }
            Array_Free(&g_logMessages);
        }
        g_logToUi = false;
    }
}

void Logger_Free(void)
{
    if (!g_loggerInitialized)
        return;

    if (g_logFile) {
        fclose(g_logFile);
        g_logFile = NULL;
    }
    free(g_logFilePath);
    g_logFilePath = NULL;

    if (g_logToUi) {
        for (size_t i = 0; i < Array_Size(&g_logMessages); i++) {
            char* msg = Array_Get(&g_logMessages, char*, i);
            free(msg);
        }
        Array_Free(&g_logMessages);
    }

    g_loggerInitialized = false;
    g_logToFile = false;
    g_logToUi = false;
}

void Logger_Log(LogLevel level, const char* file, int line, const char* format, ...)
{
    if (!g_loggerInitialized)
        return;
    if (level < g_logLevel)
        return;

    static bool s_inLog = false;
    if (s_inLog)
        return;
    s_inLog = true;

    // Get time info
    time_t rawtime;
    struct tm* timeinfo;
    char timeBuf[32];
    time(&rawtime);
    timeinfo = localtime(&rawtime);
    if (timeinfo) {
        strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", timeinfo);
    } else {
        strcpy(timeBuf, "0000-00-00 00:00:00");
    }

    // Format log message
    char msgBuf[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(msgBuf, sizeof(msgBuf), format, args);
    va_end(args);

    // Combine into final log line
    // Strip absolute workspace paths to keep it clean (e.g. show "src/main.c" instead of "/home/.../src/main.c")
    const char* relativeFile = file;
    const char* srcOccur = strstr(file, "src/");
    if (srcOccur) {
        relativeFile = srcOccur;
    } else {
        const char* testsOccur = strstr(file, "tests/");
        if (testsOccur) {
            relativeFile = testsOccur;
        }
    }

    char lineBuf[2048];
    snprintf(lineBuf, sizeof(lineBuf), "%s [%s] [%s:%d] %s", timeBuf, Logger_LevelToString(level), relativeFile, line, msgBuf);

    // 1. File output
    if (g_logToFile && g_logFile) {
        fprintf(g_logFile, "%s\n", lineBuf);
        fflush(g_logFile);
    }

    // 2. UI output (in-memory buffer)
    if (g_logToUi) {
        // Enforce limit
        while (Array_Size(&g_logMessages) >= (size_t)g_logMaxMessages) {
            char* oldest = Array_Get(&g_logMessages, char*, 0);
            free(oldest);
            for (size_t i = 0; i < Array_Size(&g_logMessages) - 1; i++) {
                char* next = Array_Get(&g_logMessages, char*, i + 1);
                void* dest = Array_RawAt(&g_logMessages, i);
                memcpy(dest, &next, sizeof(char*));
            }
            g_logMessages.size--;
        }
        char* dupLine = strdup(lineBuf);
        Array_Append(&g_logMessages, &dupLine, 1);
    }

    s_inLog = false;
}

Array* Logger_GetMessages(void)
{
    return g_logToUi ? &g_logMessages : NULL;
}
