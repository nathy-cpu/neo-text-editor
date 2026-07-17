#define _POSIX_C_SOURCE 200809L
#include "../neo.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define safe_strdup(stringValue) ((stringValue) ? strdup(stringValue) : NULL)

/**
 * @brief Clones a NULL-terminated array of strings.
 * @param sourceStringArray Source array of strings.
 * @return Dynamically allocated copy of the array of strings, or NULL.
 */
static char** CloneStringArray(char** sourceStringArray)
{
    if (!sourceStringArray)
        return NULL;
    size_t count = 0;
    while (sourceStringArray[count])
        count++;
    char** destinationStringArray = malloc((count + 1) * sizeof(char*));
    if (!destinationStringArray)
        return NULL;
    for (size_t index = 0; index < count; index++) {
        destinationStringArray[index] = strdup(sourceStringArray[index]);
    }
    destinationStringArray[count] = NULL;
    return destinationStringArray;
}

static char* defaultLogFileExtensions[] = { ".log", "*logs*", NULL };
static char* defaultLogKeywords[] = { "ERROR", "FATAL", "CRITICAL", "WARN", "WARNING", NULL };
static char* defaultLogTypes[] = { "INFO", "DEBUG", "TRACE", NULL };

static char* defaultCFileExtensions[] = { ".c", ".h", NULL };
static char* defaultCppFileExtensions[] = { ".cpp", ".hpp", ".cc", ".h", NULL };

static char* defaultCKeywords[] = { "alignas", "alignof", "auto", "break", "case", "const", "constexpr", "continue",
    "default", "do", "double", "else", "enum", "extern", "false", "float", "for", "goto", "if", "inline", "nullptr",
    "register", "restrict", "return", "sizeof", "static", "static_assert", "struct", "switch", "thread_local", "true",
    "typedef", "typeof", "typeof_unqual", "union", "void", "volatile", "while", NULL };

static char* defaultCTypes[]
    = { "int", "long", "short", "double", "float", "char", "unsigned", "signed", "bool", "size_t", "ssize_t", NULL };

static char* defaultCppKeywords[] = { "alignas", "alignof", "auto", "break", "case", "const", "constexpr", "continue",
    "default", "do", "double", "else", "enum", "extern", "false", "float", "for", "goto", "if", "inline", "nullptr",
    "register", "restrict", "return", "sizeof", "static", "static_assert", "struct", "switch", "thread_local", "true",
    "typedef", "typeof", "typeof_unqual", "union", "void", "volatile", "while", "class", "delete", "new", "namespace",
    "try", "catch", "throw", "public", "private", "protected", "virtual", "template", "typename", NULL };

static char* defaultCppTypes[] = { "int", "long", "short", "double", "float", "char", "unsigned", "signed", "bool",
    "size_t", "ssize_t", "char8_t", "char16_t", "char32_t", "wchar_t", NULL };

/**
 * @brief Initializes editor configurations to their default settings.
 * @param config Pointer to the Config struct.
 */
void Config_InitDefaults(Config* config)
{
    config->tabSize = 4;
    config->showLineNumbers = true;
    config->wrapLines = true;
    config->wrapDisableLineThreshold = 50000;
    config->syntaxEnabled = true;
    config->statusTimeout = 5;

    config->syntaxColors[HIGHLIGHT_NORMAL] = NULL;
    config->syntaxColors[HIGHLIGHT_NUMBER] = strdup("35");
    config->syntaxColors[HIGHLIGHT_MATCH] = strdup("34");
    config->syntaxColors[HIGHLIGHT_STRING] = strdup("38;5;208");
    config->syntaxColors[HIGHLIGHT_CHARACTER] = strdup("33");
    config->syntaxColors[HIGHLIGHT_COMMENT] = strdup("90");
    config->syntaxColors[HIGHLIGHT_KEYWORD] = strdup("34");
    config->syntaxColors[HIGHLIGHT_TYPE] = strdup("32");
    config->syntaxColors[HIGHLIGHT_SYMBOL] = strdup("36");

    config->keySave = CTRL_KEY('s');
    config->keyQuit = CTRL_KEY('q');
    config->keyNewTab = CTRL_KEY('t');
    config->keyCloseTab = CTRL_KEY('w');
    config->keyExplorer = CTRL_KEY('e');
    config->keyNextTab = CTRL_KEY('n');
    config->keyPrevTab = CTRL_KEY('p');
    config->keySaveAs = ALT_S;
    config->keyLogs = CTRL_KEY('l');
    config->keyToggleFold = CTRL_KEY('f');
    config->keyToggleAllFolds = ALT_F;

    config->logFile = strdup("neo.log");
    config->logLevelStr = strdup("INFO");
    config->logToFile = false;
    config->logToUi = true;
    config->logMaxMessages = 1000;
    config->undoLimit = 1000;

    // Initialize syntax database
    Array_Init(&config->syntaxDatabase, sizeof(Syntax), 4, alignof(Syntax));

    // Default C syntax rules
    Syntax cSyntax = { .fileType = strdup("C"),
        .fileMatch = CloneStringArray(defaultCFileExtensions),
        .keywords = CloneStringArray(defaultCKeywords),
        .types = CloneStringArray(defaultCTypes),
        .singleLineCommentStart = strdup("//"),
        .multiLineCommentStart = strdup("/*"),
        .multiLineCommentEnd = strdup("*/") };
    Array_Append(&config->syntaxDatabase, &cSyntax, 1);

    // Default C++ syntax rules
    Syntax cppSyntax = { .fileType = strdup("C++"),
        .fileMatch = CloneStringArray(defaultCppFileExtensions),
        .keywords = CloneStringArray(defaultCppKeywords),
        .types = CloneStringArray(defaultCppTypes),
        .singleLineCommentStart = strdup("//"),
        .multiLineCommentStart = strdup("/*"),
        .multiLineCommentEnd = strdup("*/") };
    Array_Append(&config->syntaxDatabase, &cppSyntax, 1);

    // Default Log syntax rules
    Syntax logSyntax = { .fileType = strdup("Log"),
        .fileMatch = CloneStringArray(defaultLogFileExtensions),
        .keywords = CloneStringArray(defaultLogKeywords),
        .types = CloneStringArray(defaultLogTypes),
        .singleLineCommentStart = NULL,
        .multiLineCommentStart = NULL,
        .multiLineCommentEnd = NULL };
    Array_Append(&config->syntaxDatabase, &logSyntax, 1);

    LOG_INFO("Config_InitDefaults: Initialized default configurations.");
}

/**
 * @brief Frees all dynamically allocated memory inside a Syntax struct.
 * @param syntax Pointer to the Syntax struct.
 */
void Syntax_Free(Syntax* syntax)
{
    free(syntax->fileType);
    if (syntax->fileMatch) {
        for (int index = 0; syntax->fileMatch[index]; index++)
            free(syntax->fileMatch[index]);
        free(syntax->fileMatch);
    }
    if (syntax->keywords) {
        for (int index = 0; syntax->keywords[index]; index++)
            free(syntax->keywords[index]);
        free(syntax->keywords);
    }
    if (syntax->types) {
        for (int index = 0; syntax->types[index]; index++)
            free(syntax->types[index]);
        free(syntax->types);
    }
    free(syntax->singleLineCommentStart);
    free(syntax->multiLineCommentStart);
    free(syntax->multiLineCommentEnd);
}

/**
 * @brief Frees all dynamically allocated memory inside a Config struct.
 * @param config Pointer to the Config struct.
 */
void Config_Free(Config* config)
{
    LOG_INFO("Config_Free: Freeing editor configuration.");
    for (int index = 0; index < 9; index++) {
        free(config->syntaxColors[index]);
        config->syntaxColors[index] = NULL;
    }

    for (size_t index = 0; index < Array_Size(&config->syntaxDatabase); index++) {
        Syntax* syntax = (Syntax*)Array_At(&config->syntaxDatabase, index);
        Syntax_Free(syntax);
    }
    Array_Free(&config->syntaxDatabase);

    free(config->logFile);
    config->logFile = NULL;
    free(config->logLevelStr);
    config->logLevelStr = NULL;
}

/**
 * @brief Parses a string description of a keybinding to its integer code.
 * @param keybindingString The keybinding string (e.g. "ctrl-s", "alt-s", "delete").
 * @param defaultKeyValue Default value to return if parsing fails.
 * @return Integer key code.
 */
static int ParseKeybinding(const char* keybindingString, int defaultKeyValue)
{
    if (!keybindingString)
        return defaultKeyValue;
    if (strcasecmp(keybindingString, "backspace") == 0)
        return BACKSPACE;
    if (strcasecmp(keybindingString, "delete") == 0)
        return DELETE_KEY;
    if (strcasecmp(keybindingString, "home") == 0)
        return HOME_KEY;
    if (strcasecmp(keybindingString, "end") == 0)
        return END_KEY;
    if (strcasecmp(keybindingString, "pageup") == 0)
        return PAGE_UP;
    if (strcasecmp(keybindingString, "pagedown") == 0)
        return PAGE_DOWN;
    if (strcasecmp(keybindingString, "arrow_left") == 0)
        return ARROW_LEFT;
    if (strcasecmp(keybindingString, "arrow_right") == 0)
        return ARROW_RIGHT;
    if (strcasecmp(keybindingString, "arrow_up") == 0)
        return ARROW_UP;
    if (strcasecmp(keybindingString, "arrow_down") == 0)
        return ARROW_DOWN;

    if (strncasecmp(keybindingString, "ctrl-", 5) == 0 && strlen(keybindingString) == 6) {
        char characterCode = tolower(keybindingString[5]);
        return CTRL_KEY(characterCode);
    }
    if (strncasecmp(keybindingString, "alt-", 4) == 0 && strlen(keybindingString) == 5) {
        char characterCode = tolower(keybindingString[4]);
        if (characterCode == 's')
            return ALT_S;
        if (characterCode == 'f')
            return ALT_F;
    }

    return defaultKeyValue;
}

/**
 * @brief Retrieves a string array from a Lua table.
 * @param luaState Pointer to the Lua state.
 * @param stackIndex Stack index of the Lua table.
 * @return Dynamically allocated NULL-terminated string array, or NULL.
 */
static char** GetLuaStringArray(lua_State* luaState, int stackIndex)
{
    if (!lua_istable(luaState, stackIndex))
        return NULL;
    size_t count = lua_rawlen(luaState, stackIndex);
    char** stringArray = malloc((count + 1) * sizeof(char*));
    if (!stringArray)
        return NULL;
    for (size_t index = 1; index <= count; index++) {
        lua_rawgeti(luaState, stackIndex, index);
        if (lua_isstring(luaState, -1)) {
            stringArray[index - 1] = strdup(lua_tostring(luaState, -1));
        } else {
            stringArray[index - 1] = NULL;
        }
        lua_pop(luaState, 1);
    }
    stringArray[count] = NULL;
    return stringArray;
}

/**
 * @brief Retrieves a string field from a Lua table.
 * @param luaState Pointer to the Lua state.
 * @param stackIndex Stack index of the Lua table.
 * @param tableKey The key/field name.
 * @return Dynamically allocated string copy, or NULL.
 */
static char* GetLuaTableString(lua_State* luaState, int stackIndex, const char* tableKey)
{
    lua_getfield(luaState, stackIndex, tableKey);
    char* stringValue = NULL;
    if (lua_isstring(luaState, -1)) {
        stringValue = strdup(lua_tostring(luaState, -1));
    }
    lua_pop(luaState, 1);
    return stringValue;
}

/**
 * @brief Retrieves a global integer variable from the Lua state.
 * @param luaState Pointer to the Lua state.
 * @param variableName Name of the global variable.
 * @param defaultIntegerValue Default value to return if not found/invalid.
 * @return The integer value.
 */
static int GetLuaInt(lua_State* luaState, const char* variableName, int defaultIntegerValue)
{
    lua_getglobal(luaState, variableName);
    int integerValue = defaultIntegerValue;
    if (lua_isinteger(luaState, -1)) {
        integerValue = (int)lua_tointeger(luaState, -1);
    } else if (lua_isnumber(luaState, -1)) {
        integerValue = (int)lua_tonumber(luaState, -1);
    }
    lua_pop(luaState, 1);
    return integerValue;
}

/**
 * @brief Retrieves a global boolean variable from the Lua state.
 * @param luaState Pointer to the Lua state.
 * @param variableName Name of the global variable.
 * @param defaultBooleanValue Default value to return if not found/invalid.
 * @return The boolean value.
 */
static bool GetLuaBool(lua_State* luaState, const char* variableName, bool defaultBooleanValue)
{
    lua_getglobal(luaState, variableName);
    bool booleanValue = defaultBooleanValue;
    if (lua_isboolean(luaState, -1)) {
        booleanValue = lua_toboolean(luaState, -1);
    }
    lua_pop(luaState, 1);
    return booleanValue;
}

/**
 * @brief Retrieves a global string variable from the Lua state.
 * @param luaState Pointer to the Lua state.
 * @param variableName Name of the global variable.
 * @param defaultStringValue Default value to return if not found/invalid.
 * @return Dynamically allocated copy of the string, or default/NULL.
 */
static char* GetLuaString(lua_State* luaState, const char* variableName, const char* defaultStringValue)
{
    lua_getglobal(luaState, variableName);
    char* stringValue = NULL;
    if (lua_isstring(luaState, -1)) {
        stringValue = strdup(lua_tostring(luaState, -1));
    } else if (defaultStringValue) {
        stringValue = strdup(defaultStringValue);
    }
    lua_pop(luaState, 1);
    return stringValue;
}

/**
 * @brief Retrieves a keybinding value from a Lua table.
 * @param luaState Pointer to the Lua state.
 * @param tableName Name of the global keybindings table.
 * @param tableKey Key name inside the table.
 * @param defaultKeyValue Default keybinding integer code.
 * @return Integer key code.
 */
static int GetLuaTableKeybinding(lua_State* luaState, const char* tableName, const char* tableKey, int defaultKeyValue)
{
    lua_getglobal(luaState, tableName);
    int keyValue = defaultKeyValue;
    if (lua_istable(luaState, -1)) {
        lua_getfield(luaState, -1, tableKey);
        if (lua_isstring(luaState, -1)) {
            keyValue = ParseKeybinding(lua_tostring(luaState, -1), defaultKeyValue);
        }
        lua_pop(luaState, 1);
    }
    lua_pop(luaState, 1);
    return keyValue;
}

/**
 * @brief Retrieves a color value string from a Lua table.
 * @param luaState Pointer to the Lua state.
 * @param tableName Name of the global colors table.
 * @param tableKey Key name inside the table.
 * @param defaultColorValue Default color string.
 * @return Dynamically allocated copy of the color string, or NULL.
 */
static char* GetLuaTableColor(lua_State* luaState, const char* tableName, const char* tableKey, char* defaultColorValue)
{
    lua_getglobal(luaState, tableName);
    char* colorValue = NULL;
    const char* stringValue = defaultColorValue;
    if (lua_istable(luaState, -1)) {
        lua_getfield(luaState, -1, tableKey);
        if (lua_isstring(luaState, -1)) {
            stringValue = lua_tostring(luaState, -1);
        }
        lua_pop(luaState, 1);
    }
    lua_pop(luaState, 1);
    if (stringValue) {
        colorValue = strdup(stringValue);
    }
    return colorValue;
}

/**
 * @brief Loads config.lua and applies options, colors, and language structures.
 * @param editor Pointer to the Editor struct.
 * @param configFilePath Initial config file path to check.
 * @return True on successful parse, false otherwise.
 */
bool Editor_LoadConfig(Editor* editor, const char* configFilePath)
{
    LOG_INFO("Loading editor configuration from: %s", configFilePath);
    char resolvedConfigPath[1024];
    struct stat fileStat;

    // Check path directly (or default local)
    if (stat(configFilePath, &fileStat) == 0) {
        strncpy(resolvedConfigPath, configFilePath, sizeof(resolvedConfigPath) - 1);
        resolvedConfigPath[sizeof(resolvedConfigPath) - 1] = '\0';
    } else {
        // Try home directory
        char* homeDir = getenv("HOME");
        if (homeDir) {
            snprintf(resolvedConfigPath, sizeof(resolvedConfigPath), "%s/.config/neo/config.lua", homeDir);
            if (stat(resolvedConfigPath, &fileStat) != 0) {
                // Config file not found, use defaults
                LOG_WARN("Config file not found in home directory: %s", resolvedConfigPath);
                return false;
            }
        } else {
            return false;
        }
    }

    lua_State* luaState = luaL_newstate();
    if (!luaState) {
        LOG_ERROR("Failed to create Lua state for configuration parsing.");
        Editor_SetStatusMessage(editor, "Error: Failed to create Lua state for configuration.");
        return false;
    }
    luaL_openlibs(luaState);

    if (luaL_dofile(luaState, resolvedConfigPath) != LUA_OK) {
        const char* errorMessage = lua_tostring(luaState, -1);
        LOG_ERROR("Lua config file execution failed: %s", errorMessage);
        Editor_SetStatusMessage(editor, "Lua Config Error: %.100s", errorMessage);
        lua_close(luaState);
        return false;
    }

    Config* config = &editor->config;

    // Parse scalar options
    config->tabSize = GetLuaInt(luaState, "tab_size", config->tabSize);
    config->showLineNumbers = GetLuaBool(luaState, "show_line_numbers", config->showLineNumbers);
    config->wrapLines = GetLuaBool(luaState, "wrap_lines", config->wrapLines);
    config->wrapDisableLineThreshold
        = (size_t)GetLuaInt(luaState, "wrap_disable_line_threshold", (int)config->wrapDisableLineThreshold);
    config->syntaxEnabled = GetLuaBool(luaState, "syntax_enabled", config->syntaxEnabled);
    config->statusTimeout = GetLuaInt(luaState, "status_timeout", config->statusTimeout);

    char* newLogFile = GetLuaString(luaState, "log_file", config->logFile);
    if (newLogFile) {
        free(config->logFile);
        config->logFile = newLogFile;
    }
    char* newLogLevel = GetLuaString(luaState, "log_level", config->logLevelStr);
    if (newLogLevel) {
        free(config->logLevelStr);
        config->logLevelStr = newLogLevel;
    }
    config->logToFile = GetLuaBool(luaState, "log_to_file", config->logToFile);
    config->logToUi = GetLuaBool(luaState, "log_to_ui", config->logToUi);
    config->logMaxMessages = GetLuaInt(luaState, "log_max_messages", config->logMaxMessages);
    config->undoLimit = (size_t)GetLuaInt(luaState, "undo_limit", (int)config->undoLimit);

    // Parse syntax colors
    char* newSyntaxColors[9];
    newSyntaxColors[HIGHLIGHT_NORMAL] = NULL;
    newSyntaxColors[HIGHLIGHT_NUMBER]
        = GetLuaTableColor(luaState, "colors", "number", config->syntaxColors[HIGHLIGHT_NUMBER]);
    newSyntaxColors[HIGHLIGHT_MATCH]
        = GetLuaTableColor(luaState, "colors", "match", config->syntaxColors[HIGHLIGHT_MATCH]);
    newSyntaxColors[HIGHLIGHT_STRING]
        = GetLuaTableColor(luaState, "colors", "string", config->syntaxColors[HIGHLIGHT_STRING]);
    newSyntaxColors[HIGHLIGHT_CHARACTER]
        = GetLuaTableColor(luaState, "colors", "character", config->syntaxColors[HIGHLIGHT_CHARACTER]);
    newSyntaxColors[HIGHLIGHT_COMMENT]
        = GetLuaTableColor(luaState, "colors", "comment", config->syntaxColors[HIGHLIGHT_COMMENT]);
    newSyntaxColors[HIGHLIGHT_KEYWORD]
        = GetLuaTableColor(luaState, "colors", "keyword", config->syntaxColors[HIGHLIGHT_KEYWORD]);
    newSyntaxColors[HIGHLIGHT_TYPE]
        = GetLuaTableColor(luaState, "colors", "type", config->syntaxColors[HIGHLIGHT_TYPE]);
    newSyntaxColors[HIGHLIGHT_SYMBOL]
        = GetLuaTableColor(luaState, "colors", "symbol", config->syntaxColors[HIGHLIGHT_SYMBOL]);

    for (int index = 0; index < 9; index++) {
        if (newSyntaxColors[index]) {
            free(config->syntaxColors[index]);
            config->syntaxColors[index] = newSyntaxColors[index];
        }
    }

    // Parse keybindings
    config->keySave = GetLuaTableKeybinding(luaState, "keybindings", "save", config->keySave);
    config->keyQuit = GetLuaTableKeybinding(luaState, "keybindings", "quit", config->keyQuit);
    config->keyNewTab = GetLuaTableKeybinding(luaState, "keybindings", "new_tab", config->keyNewTab);
    config->keyCloseTab = GetLuaTableKeybinding(luaState, "keybindings", "close_tab", config->keyCloseTab);
    config->keyExplorer = GetLuaTableKeybinding(luaState, "keybindings", "explorer", config->keyExplorer);
    config->keyNextTab = GetLuaTableKeybinding(luaState, "keybindings", "next_tab", config->keyNextTab);
    config->keyPrevTab = GetLuaTableKeybinding(luaState, "keybindings", "prev_tab", config->keyPrevTab);
    config->keySaveAs = GetLuaTableKeybinding(luaState, "keybindings", "save_as", config->keySaveAs);
    config->keyLogs = GetLuaTableKeybinding(luaState, "keybindings", "show_logs", config->keyLogs);
    config->keyToggleFold = GetLuaTableKeybinding(luaState, "keybindings", "toggle_fold", config->keyToggleFold);
    config->keyToggleAllFolds
        = GetLuaTableKeybinding(luaState, "keybindings", "toggle_all_folds", config->keyToggleAllFolds);

    // Parse languages
    lua_getglobal(luaState, "languages");
    if (lua_istable(luaState, -1)) {
        size_t count = lua_rawlen(luaState, -1);
        for (size_t index = 1; index <= count; index++) {
            lua_rawgeti(luaState, -1, index);
            if (lua_istable(luaState, -1)) {
                Syntax syntax = { 0 };
                syntax.fileType = GetLuaTableString(luaState, -1, "name");

                lua_getfield(luaState, -1, "extensions");
                syntax.fileMatch = GetLuaStringArray(luaState, -1);
                lua_pop(luaState, 1);

                lua_getfield(luaState, -1, "keywords");
                syntax.keywords = GetLuaStringArray(luaState, -1);
                lua_pop(luaState, 1);

                lua_getfield(luaState, -1, "types");
                syntax.types = GetLuaStringArray(luaState, -1);
                lua_pop(luaState, 1);

                syntax.singleLineCommentStart = GetLuaTableString(luaState, -1, "single_line_comment");
                syntax.multiLineCommentStart = GetLuaTableString(luaState, -1, "multi_line_comment_start");
                syntax.multiLineCommentEnd = GetLuaTableString(luaState, -1, "multi_line_comment_end");

                if (syntax.fileType && syntax.fileMatch) {
                    bool isLanguageReplaced = false;
                    for (size_t dbIndex = 0; dbIndex < Array_Size(&config->syntaxDatabase); dbIndex++) {
                        Syntax* existingLanguageSyntax = (Syntax*)Array_At(&config->syntaxDatabase, dbIndex);
                        if (strcasecmp(existingLanguageSyntax->fileType, syntax.fileType) == 0) {
                            Syntax_Free(existingLanguageSyntax);
                            *existingLanguageSyntax = syntax;
                            isLanguageReplaced = true;
                            break;
                        }
                    }
                    if (!isLanguageReplaced) {
                        Array_Append(&config->syntaxDatabase, &syntax, 1);
                    }
                    LOG_INFO("Editor_LoadConfig: registered/updated language rules for: %s", syntax.fileType);
                } else {
                    Syntax_Free(&syntax);
                }
            }
            lua_pop(luaState, 1);
        }
    }
    lua_pop(luaState, 1);

    lua_close(luaState);
    LOG_INFO("Successfully loaded config file: %s", resolvedConfigPath);
    return true;
}
