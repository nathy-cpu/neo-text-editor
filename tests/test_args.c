#include "../src/neo.h"
#include <assert.h>
#include <string.h>
#include <stdbool.h>

static void test_args_basic(void)
{
    char* argv[] = { "neo", "-n", "-w", "-s", "-r", "-t", "8", "-c", "custom_config.lua" };
    int argc = sizeof(argv) / sizeof(argv[0]);

    CliOptions options;
    bool success = CliOptions_Parse(&options, argc, argv);
    assert(success);
    assert(options.overrideShowLineNumbers == 0);
    assert(options.overrideWrapLines == 0);
    assert(options.overrideSyntaxEnabled == 0);
    assert(options.readOnlyMode == true);
    assert(options.overrideTabSize == 8);
    assert(strcmp(options.configPath, "custom_config.lua") == 0);
    assert(options.fileCount == 0);

    CliOptions_Free(&options);
}

static void test_args_enablers(void)
{
    char* argv[] = { "neo", "--line-numbers", "--wrap", "--syntax" };
    int argc = sizeof(argv) / sizeof(argv[0]);

    CliOptions options;
    bool success = CliOptions_Parse(&options, argc, argv);
    assert(success);
    assert(options.overrideShowLineNumbers == 1);
    assert(options.overrideWrapLines == 1);
    assert(options.overrideSyntaxEnabled == 1);
    assert(options.fileCount == 0);

    CliOptions_Free(&options);
}

static void test_args_jumps_forward(void)
{
    char* argv[] = { "neo", "+25", "src/main.c", "+100:15", "Makefile" };
    int argc = sizeof(argv) / sizeof(argv[0]);

    CliOptions options;
    bool success = CliOptions_Parse(&options, argc, argv);
    assert(success);
    assert(options.fileCount == 2);
    assert(strcmp(options.files[0], "src/main.c") == 0);
    assert(options.fileLines[0] == 25);
    assert(options.fileColumns[0] == 0);

    assert(strcmp(options.files[1], "Makefile") == 0);
    assert(options.fileLines[1] == 100);
    assert(options.fileColumns[1] == 15);

    CliOptions_Free(&options);
}

static void test_args_jumps_backward(void)
{
    char* argv[] = { "neo", "src/main.c", "+25:8" };
    int argc = sizeof(argv) / sizeof(argv[0]);

    CliOptions options;
    bool success = CliOptions_Parse(&options, argc, argv);
    assert(success);
    assert(options.fileCount == 1);
    assert(strcmp(options.files[0], "src/main.c") == 0);
    assert(options.fileLines[0] == 25);
    assert(options.fileColumns[0] == 8);

    CliOptions_Free(&options);
}

static void test_args_help_version(void)
{
    char* argv1[] = { "neo", "-h" };
    CliOptions options1;
    assert(CliOptions_Parse(&options1, 2, argv1));
    assert(options1.helpRequested == true);
    CliOptions_Free(&options1);

    char* argv2[] = { "neo", "--version" };
    CliOptions options2;
    assert(CliOptions_Parse(&options2, 2, argv2));
    assert(options2.versionRequested == true);
    CliOptions_Free(&options2);
}

static void test_args_missing_value_errors(void)
{
    struct {
        char* argv[3];
    } cases[] = {
        { { "neo", "-c", NULL } },
        { { "neo", "--tab-size", NULL } },
        { { "neo", "--log-file", NULL } },
        { { "neo", "--log-level", NULL } },
        { { "neo", "--log-max-messages", NULL } },
        { { "neo", "--line", NULL } },
        { { "neo", "--column", NULL } },
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        CliOptions options;
        bool success = CliOptions_Parse(&options, 2, cases[i].argv);
        assert(success == false && "An option requiring a value with none supplied must fail to parse");
        CliOptions_Free(&options);
    }
}

static void test_args_invalid_value_errors(void)
{
    char* tabSizeZero[] = { "neo", "--tab-size", "0" };
    CliOptions options1;
    assert(CliOptions_Parse(&options1, 3, tabSizeZero) == false);
    CliOptions_Free(&options1);

    char* tabSizeNegative[] = { "neo", "-t", "-5" };
    CliOptions options2;
    assert(CliOptions_Parse(&options2, 3, tabSizeNegative) == false);
    CliOptions_Free(&options2);

    char* logMaxZero[] = { "neo", "--log-max-messages", "0" };
    CliOptions options3;
    assert(CliOptions_Parse(&options3, 3, logMaxZero) == false);
    CliOptions_Free(&options3);

    char* lineZero[] = { "neo", "--line", "0" };
    CliOptions options4;
    assert(CliOptions_Parse(&options4, 3, lineZero) == false);
    CliOptions_Free(&options4);

    char* columnNegative[] = { "neo", "--column", "-1" };
    CliOptions options5;
    assert(CliOptions_Parse(&options5, 3, columnNegative) == false);
    CliOptions_Free(&options5);
}

static void test_args_unknown_option(void)
{
    char* argv[] = { "neo", "--not-a-real-option" };
    CliOptions options;
    assert(CliOptions_Parse(&options, 2, argv) == false);
    CliOptions_Free(&options);
}

static void test_args_log_overrides(void)
{
    char* argv[] = { "neo", "--log-file", "custom.log", "--log-level", "DEBUG", "--log-to-file", "--log-to-ui",
        "--log-max-messages", "500" };
    int argc = sizeof(argv) / sizeof(argv[0]);

    CliOptions options;
    assert(CliOptions_Parse(&options, argc, argv) == true);
    assert(strcmp(options.overrideLogFile, "custom.log") == 0);
    assert(strcmp(options.overrideLogLevel, "DEBUG") == 0);
    assert(options.overrideLogToFile == 1);
    assert(options.overrideLogToUi == 1);
    assert(options.overrideLogMaxMessages == 500);
    CliOptions_Free(&options);

    char* argvOff[] = { "neo", "--no-log-to-file", "--no-log-to-ui" };
    CliOptions optionsOff;
    assert(CliOptions_Parse(&optionsOff, 3, argvOff) == true);
    assert(optionsOff.overrideLogToFile == 0);
    assert(optionsOff.overrideLogToUi == 0);
    CliOptions_Free(&optionsOff);
}

static void test_args_no_args(void)
{
    char* argv[] = { "neo" };
    CliOptions options;
    assert(CliOptions_Parse(&options, 1, argv) == true);
    assert(options.fileCount == 0);
    assert(options.configPath == NULL);
    assert(options.overrideTabSize == -1);
    assert(options.overrideLogFile == NULL);
    CliOptions_Free(&options);
}

static void test_args_bare_plus_is_a_filename(void)
{
    // A lone "+" has no digit after it, so it falls through to the positional
    // filename branch instead of being parsed as a +line jump.
    char* argv[] = { "neo", "+" };
    CliOptions options;
    assert(CliOptions_Parse(&options, 2, argv) == true);
    assert(options.fileCount == 1);
    assert(strcmp(options.files[0], "+") == 0);
    CliOptions_Free(&options);
}

static void test_args_trailing_jump_with_no_file_is_dropped(void)
{
    // A +line jump with no file at all (before or after) has nothing to attach
    // to, so it's silently dropped rather than crashing or fabricating a file.
    char* argv[] = { "neo", "+42" };
    CliOptions options;
    assert(CliOptions_Parse(&options, 2, argv) == true);
    assert(options.fileCount == 0);
    CliOptions_Free(&options);
}

static void test_args_free_resets_state(void)
{
    char* argv[] = { "neo", "a.c", "b.c" };
    CliOptions options;
    assert(CliOptions_Parse(&options, 3, argv) == true);
    assert(options.fileCount == 2);

    CliOptions_Free(&options);
    assert(options.files == NULL);
    assert(options.fileLines == NULL);
    assert(options.fileColumns == NULL);
    assert(options.fileCount == 0);
}
