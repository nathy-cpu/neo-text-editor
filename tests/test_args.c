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
