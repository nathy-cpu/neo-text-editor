#pragma once

#include <stdbool.h>

typedef struct {
    const char* configPath;
    int overrideTabSize;
    int overrideShowLineNumbers; // -1: no override, 0: false, 1: true
    int overrideWrapLines; // -1: no override, 0: false, 1: true
    int overrideSyntaxEnabled; // -1: no override, 0: false, 1: true
    bool readOnlyMode;

    // Logging Overrides
    const char* overrideLogFile;
    const char* overrideLogLevel;
    int overrideLogToFile; // -1: no override, 0: false, 1: true
    int overrideLogToUi; // -1: no override, 0: false, 1: true
    int overrideLogMaxMessages;

    char** files;
    int* fileLines;
    int* fileColumns;
    int fileCount;

    bool helpRequested;
    bool versionRequested;
} CliOptions;

/**
 * @brief Parses command line arguments.
 */
bool CliOptions_Parse(CliOptions* options, int argc, char* argv[]);

/**
 * @brief Frees any resources allocated inside a CliOptions struct.
 */
void CliOptions_Free(CliOptions* options);

/**
 * @brief Prints usage and help screen.
 */
void PrintHelp(const char* progName);

/**
 * @brief Prints program version information.
 */
void PrintVersion(void);
