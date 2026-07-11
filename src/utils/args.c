#define _POSIX_C_SOURCE 200809L
#include "../neo.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void PrintHelp(const char* progName)
{
    printf("Usage: %s [options] [file [line_number_option] ...]\n\n", progName);
    printf("Options:\n");
    printf("  -c, --config <path>      Load configuration from <path>\n");
    printf("  -t, --tab-size <size>    Set tab width (overrides config)\n");
    printf("  -n, --no-line-numbers    Disable line numbers (overrides config)\n");
    printf("  --line-numbers           Enable line numbers (overrides config)\n");
    printf("  -w, --no-wrap            Disable line wrapping (overrides config)\n");
    printf("  --wrap                   Enable line wrapping (overrides config)\n");
    printf("  -s, --no-syntax          Disable syntax highlighting (overrides config)\n");
    printf("  --syntax                 Enable syntax highlighting (overrides config)\n");
    printf("  -r, --read-only          Open files in read-only mode\n");
    printf("  --log-file <path>        Set log file path (overrides config)\n");
    printf("  --log-level <level>      Set log level (DEBUG, INFO, WARN, ERROR, FATAL)\n");
    printf("  --log-to-file            Enable logging to file\n");
    printf("  --no-log-to-file         Disable logging to file\n");
    printf("  --log-to-ui              Enable log viewer overlay in UI\n");
    printf("  --no-log-to-ui           Disable log viewer overlay in UI\n");
    printf("  --log-max-messages <num> Set maximum log message buffer size\n");
    printf("  -h, --help               Display this help message and exit\n");
    printf("  -v, --version            Display version information and exit\n");
    printf("  +<line>[:<col>]          Jump to specific line and column (e.g. +45:10)\n");
    printf("  --line <number>          Jump to specific line number\n");
    printf("  --column <number>        Jump to specific column number\n");
}

void PrintVersion(void) { printf("Neo Text Editor v1.0.0\n"); }

bool CliOptions_Parse(CliOptions* options, int argc, char* argv[])
{
    // Initialize defaults
    options->configPath = NULL;
    options->overrideTabSize = -1;
    options->overrideShowLineNumbers = -1;
    options->overrideWrapLines = -1;
    options->overrideSyntaxEnabled = -1;
    options->readOnlyMode = false;
    options->overrideLogFile = NULL;
    options->overrideLogLevel = NULL;
    options->overrideLogToFile = -1;
    options->overrideLogToUi = -1;
    options->overrideLogMaxMessages = -1;
    options->files = malloc(argc * sizeof(char*));
    options->fileLines = malloc(argc * sizeof(int));
    options->fileColumns = malloc(argc * sizeof(int));
    options->fileCount = 0;
    options->helpRequested = false;
    options->versionRequested = false;

    if (!options->files || !options->fileLines || !options->fileColumns) {
        return false;
    }

    int pendingLineNumber = 0;
    int pendingColumnNumber = 0;

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '+' && isdigit((unsigned char)argv[i][1])) {
            // Parse +line[:col] without mutating read-only argv strings
            char temp[128];
            strncpy(temp, argv[i], sizeof(temp) - 1);
            temp[sizeof(temp) - 1] = '\0';
            char* colon = strchr(temp, ':');
            if (colon) {
                *colon = '\0';
                pendingLineNumber = atoi(temp + 1);
                pendingColumnNumber = atoi(colon + 1);
            } else {
                pendingLineNumber = atoi(temp + 1);
                pendingColumnNumber = 0;
            }
        } else if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--config") == 0) {
            if (i + 1 < argc) {
                options->configPath = argv[++i];
            } else {
                fprintf(stderr, "Error: %s requires a path argument\n", argv[i]);
                return false;
            }
        } else if (strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--tab-size") == 0) {
            if (i + 1 < argc) {
                options->overrideTabSize = atoi(argv[++i]);
                if (options->overrideTabSize <= 0) {
                    fprintf(stderr, "Error: Invalid tab size '%s'\n", argv[i]);
                    return false;
                }
            } else {
                fprintf(stderr, "Error: %s requires a size argument\n", argv[i]);
                return false;
            }
        } else if (strcmp(argv[i], "-n") == 0 || strcmp(argv[i], "--no-line-numbers") == 0) {
            options->overrideShowLineNumbers = 0;
        } else if (strcmp(argv[i], "--line-numbers") == 0) {
            options->overrideShowLineNumbers = 1;
        } else if (strcmp(argv[i], "-w") == 0 || strcmp(argv[i], "--no-wrap") == 0) {
            options->overrideWrapLines = 0;
        } else if (strcmp(argv[i], "--wrap") == 0) {
            options->overrideWrapLines = 1;
        } else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--no-syntax") == 0) {
            options->overrideSyntaxEnabled = 0;
        } else if (strcmp(argv[i], "--syntax") == 0) {
            options->overrideSyntaxEnabled = 1;
        } else if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--read-only") == 0) {
            options->readOnlyMode = true;
        } else if (strcmp(argv[i], "--log-file") == 0) {
            if (i + 1 < argc) {
                options->overrideLogFile = argv[++i];
            } else {
                fprintf(stderr, "Error: --log-file requires a path argument\n");
                return false;
            }
        } else if (strcmp(argv[i], "--log-level") == 0) {
            if (i + 1 < argc) {
                options->overrideLogLevel = argv[++i];
            } else {
                fprintf(stderr, "Error: --log-level requires a level argument (DEBUG, INFO, WARN, ERROR, FATAL)\n");
                return false;
            }
        } else if (strcmp(argv[i], "--log-to-file") == 0) {
            options->overrideLogToFile = 1;
        } else if (strcmp(argv[i], "--no-log-to-file") == 0) {
            options->overrideLogToFile = 0;
        } else if (strcmp(argv[i], "--log-to-ui") == 0) {
            options->overrideLogToUi = 1;
        } else if (strcmp(argv[i], "--no-log-to-ui") == 0) {
            options->overrideLogToUi = 0;
        } else if (strcmp(argv[i], "--log-max-messages") == 0) {
            if (i + 1 < argc) {
                options->overrideLogMaxMessages = atoi(argv[++i]);
                if (options->overrideLogMaxMessages <= 0) {
                    fprintf(stderr, "Error: Invalid log max messages '%s'\n", argv[i]);
                    return false;
                }
            } else {
                fprintf(stderr, "Error: --log-max-messages requires an integer argument\n");
                return false;
            }
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            options->helpRequested = true;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            options->versionRequested = true;
        } else if (strcmp(argv[i], "--line") == 0) {
            if (i + 1 < argc) {
                pendingLineNumber = atoi(argv[++i]);
                if (pendingLineNumber <= 0) {
                    fprintf(stderr, "Error: Invalid line number '%s'\n", argv[i]);
                    return false;
                }
            } else {
                fprintf(stderr, "Error: --line requires a number argument\n");
                return false;
            }
        } else if (strcmp(argv[i], "--column") == 0) {
            if (i + 1 < argc) {
                pendingColumnNumber = atoi(argv[++i]);
                if (pendingColumnNumber <= 0) {
                    fprintf(stderr, "Error: Invalid column number '%s'\n", argv[i]);
                    return false;
                }
            } else {
                fprintf(stderr, "Error: --column requires a number argument\n");
                return false;
            }
        } else if (argv[i][0] == '-' && argv[i][1] != '\0') {
            fprintf(stderr, "Error: Unknown option '%s'\n", argv[i]);
            return false;
        } else {
            // Positional filename argument
            options->files[options->fileCount] = strdup(argv[i]);
            options->fileLines[options->fileCount] = pendingLineNumber;
            options->fileColumns[options->fileCount] = pendingColumnNumber;
            options->fileCount++;

            pendingLineNumber = 0;
            pendingColumnNumber = 0;
        }
    }

    // Apply any remaining pending line/col jump to the last file
    if ((pendingLineNumber > 0 || pendingColumnNumber > 0) && options->fileCount > 0) {
        int lastIdx = options->fileCount - 1;
        if (options->fileLines[lastIdx] == 0) {
            options->fileLines[lastIdx] = pendingLineNumber;
        }
        if (options->fileColumns[lastIdx] == 0) {
            options->fileColumns[lastIdx] = pendingColumnNumber;
        }
    }

    LOG_INFO("CliOptions_Parse: parsed %d arguments. fileCount=%d, readOnly=%d, config=%s", argc, options->fileCount,
        options->readOnlyMode, options->configPath ? options->configPath : "<default>");
    return true;
}

void CliOptions_Free(CliOptions* options)
{
    if (options) {
        if (options->files) {
            for (int i = 0; i < options->fileCount; i++) {
                free(options->files[i]);
            }
            free(options->files);
        }
        free(options->fileLines);
        free(options->fileColumns);
        options->files = NULL;
        options->fileLines = NULL;
        options->fileColumns = NULL;
        options->fileCount = 0;
    }
}
