#pragma once

#include "array.h"
#include <stdbool.h>

typedef enum { LOG_LEVEL_DEBUG = 0, LOG_LEVEL_INFO, LOG_LEVEL_WARN, LOG_LEVEL_ERROR, LOG_LEVEL_FATAL } LogLevel;

/**
 * @brief Initializes the logging system.
 */
void Logger_Init(const char* logFile, LogLevel level, bool logToFile, bool logToUi, int maxMessages);

/**
 * @brief Reconfigures the logging system.
 */
void Logger_Configure(const char* logFile, LogLevel level, bool logToFile, bool logToUi, int maxMessages);

/**
 * @brief Frees resources allocated by the logging system.
 */
void Logger_Free(void);

/**
 * @brief Logs a formatted message.
 */
void Logger_Log(LogLevel level, const char* file, int line, const char* format, ...);

/**
 * @brief Returns the in-memory array of log messages (Array of char*).
 */
Array* Logger_GetMessages(void);

/**
 * @brief Parses a string log level to its LogLevel enum value.
 */
LogLevel Logger_ParseLevel(const char* levelStr, LogLevel defaultLevel);

/**
 * @brief Converts a LogLevel enum to its string representation.
 */
const char* Logger_LevelToString(LogLevel level);

// Logging Macros
#define LOG_DEBUG(...) Logger_Log(LOG_LEVEL_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_INFO(...) Logger_Log(LOG_LEVEL_INFO, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_WARN(...) Logger_Log(LOG_LEVEL_WARN, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...) Logger_Log(LOG_LEVEL_ERROR, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_FATAL(...) Logger_Log(LOG_LEVEL_FATAL, __FILE__, __LINE__, __VA_ARGS__)
