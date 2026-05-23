#include "../neo.h"
#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

char* cFileExtensions[] = { ".c", ".h", NULL };
char* cppFileExtensions[] = { ".cpp", ".hpp", ".cc", ".h", NULL };

char* cKeywords[] = { "alignas", "alignof", "auto", "break", "case", "const", "constexpr", "continue", "default", "do",
    "double", "else", "enum", "extern", "false", "float", "for", "goto", "if", "inline", "nullptr", "register",
    "restrict", "return", "sizeof", "static", "static_assert", "struct", "switch", "thread_local", "true", "typedef",
    "typeof", "typeof_unqual", "union", "void", "volatile", "while", NULL };

char* cTypes[]
    = { "int", "long", "short", "double", "float", "char", "unsigned", "signed", "bool", "size_t", "ssize_t", NULL };

char* cppKeywords[] = { "alignas", "alignof", "auto", "break", "case", "const", "constexpr", "continue", "default",
    "do", "double", "else", "enum", "extern", "false", "float", "for", "goto", "if", "inline", "nullptr", "register",
    "restrict", "return", "sizeof", "static", "static_assert", "struct", "switch", "thread_local", "true", "typedef",
    "typeof", "typeof_unqual", "union", "void", "volatile", "while", "class", "delete", "new", "namespace", "try",
    "catch", "throw", "public", "private", "protected", "virtual", "template", "typename", NULL };

char* cppTypes[] = { "int", "long", "short", "double", "float", "char", "unsigned", "signed", "bool", "size_t",
    "ssize_t", "char8_t", "char16_t", "char32_t", "wchar_t", NULL };

Syntax syntaxDatabase[] = { { "C", cFileExtensions, cKeywords, cTypes, "//", "/*", "*/" },
    { "C++", cppFileExtensions, cppKeywords, cppTypes, "//", "/*", "*/" },
    { NULL, NULL, NULL, NULL, NULL, NULL, NULL } };

char* GetSyntaxColor(HighlightType highlight)
{
    switch (highlight) {
    case HIGHLIGHT_NORMAL:
        return NULL;
    case HIGHLIGHT_NUMBER:
        return "31";
    case HIGHLIGHT_MATCH:
        return "34";
    case HIGHLIGHT_STRING:
        return "35";
    case HIGHLIGHT_COMMENT:
        return "36";
    case HIGHLIGHT_KEYWORD:
        return "33";
    case HIGHLIGHT_TYPE:
        return "32";
    default:
        return "39";
    }
}

static bool IsSeparator(int c) { return isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>[];", c) != NULL; }

static void UpdateLineSyntax(Line* line, Syntax* syntax, bool* inMultiLineComment)
{
    GapBuffer_Clear(&line->styles);

    Slice textSlice = GapBuffer_ToSlice(&line->text);
    size_t length = GapBuffer_Size(&line->text);
    char* text = (char*)textSlice.data;

    // Default to HIGHLIGHT_NORMAL
    for (size_t i = 0; i < length; i++) {
        GapBuffer_InsertChar(&line->styles, i, HIGHLIGHT_NORMAL);
    }

    Slice stylesSlice = GapBuffer_ToSlice(&line->styles);
    char* styles = (char*)stylesSlice.data;

    if (syntax == NULL)
        return;

    int singleCommentLength = syntax->singleLineCommentStart ? strlen(syntax->singleLineCommentStart) : 0;
    int multiCommentStartLength = syntax->multiLineCommentStart ? strlen(syntax->multiLineCommentStart) : 0;
    int multiCommentEndLength = syntax->multiLineCommentEnd ? strlen(syntax->multiLineCommentEnd) : 0;

    bool previousSeparator = true;
    int inString = 0;
    bool inSingleString = false;

    size_t i = 0;
    while (i < length) {
        char c = text[i];

        // Strings
        if (!(*inMultiLineComment)) {
            if (c == '"' || c == '\'') {
                if (inString && (inSingleString ? c == '\'' : c == '"')) {
                    styles[i] = HIGHLIGHT_STRING;
                    inString = 0;
                    i++;
                    continue;
                } else if (!inString) {
                    inString = 1;
                    inSingleString = (c == '\'');
                    styles[i] = HIGHLIGHT_STRING;
                    i++;
                    continue;
                }
            }
        }

        if (inString) {
            styles[i] = HIGHLIGHT_STRING;
            if (c == '\\' && i + 1 < length) { // Escape sequence
                styles[i + 1] = HIGHLIGHT_STRING;
                i += 2;
                continue;
            }
            i++;
            continue;
        }

        // Single line comment
        if (singleCommentLength && !(*inMultiLineComment)) {
            if (!strncmp(&text[i], syntax->singleLineCommentStart, singleCommentLength)) {
                memset(&styles[i], HIGHLIGHT_COMMENT, length - i);
                break;
            }
        }

        // Multi-line comment
        if (multiCommentStartLength && multiCommentEndLength) {
            if (*inMultiLineComment) {
                styles[i] = HIGHLIGHT_COMMENT;
                if (!strncmp(&text[i], syntax->multiLineCommentEnd, multiCommentEndLength)) {
                    memset(&styles[i], HIGHLIGHT_COMMENT, multiCommentEndLength);
                    i += multiCommentEndLength;
                    *inMultiLineComment = false;
                    previousSeparator = true;
                    continue;
                }
                i++;
                continue;
            } else if (!strncmp(&text[i], syntax->multiLineCommentStart, multiCommentStartLength)) {
                memset(&styles[i], HIGHLIGHT_COMMENT, multiCommentStartLength);
                i += multiCommentStartLength;
                *inMultiLineComment = true;
                continue;
            }
        }

        // Numbers
        if ((isdigit(c) && (previousSeparator || (i > 0 && styles[i - 1] == HIGHLIGHT_NUMBER)))
            || (c == '.' && i > 0 && styles[i - 1] == HIGHLIGHT_NUMBER)) {
            styles[i] = HIGHLIGHT_NUMBER;
            previousSeparator = false;
            i++;
            continue;
        }

        if (syntax->keywords || syntax->types) {
            if (previousSeparator) {
                bool matched = false;
                if (syntax->keywords) {
                    for (int j = 0; syntax->keywords[j]; j++) {
                        int keywordLength = strlen(syntax->keywords[j]);
                        if (!strncmp(&text[i], syntax->keywords[j], keywordLength)
                            && (i + keywordLength == length || IsSeparator(text[i + keywordLength]))) {
                            memset(&styles[i], HIGHLIGHT_KEYWORD, keywordLength);
                            i += keywordLength;
                            matched = true;
                            break;
                        }
                    }
                }
                if (!matched && syntax->types) {
                    for (int j = 0; syntax->types[j]; j++) {
                        int keywordLength = strlen(syntax->types[j]);
                        if (!strncmp(&text[i], syntax->types[j], keywordLength)
                            && (i + keywordLength == length || IsSeparator(text[i + keywordLength]))) {
                            memset(&styles[i], HIGHLIGHT_TYPE, keywordLength);
                            i += keywordLength;
                            matched = true;
                            break;
                        }
                    }
                }
                if (matched) {
                    previousSeparator = false;
                    continue;
                }
            }
        }

        previousSeparator = IsSeparator(c);
        i++;
    }
}

void Tab_UpdateSyntax(Tab* tab)
{
    if (tab->syntax == NULL)
        return;
    bool inMultiLineComment = false;
    for (size_t i = 0; i < Buffer_GetLineCount(tab->buffer); i++) {
        Line* line = Buffer_GetLine(tab->buffer, i);
        if (line) {
            UpdateLineSyntax(line, tab->syntax, &inMultiLineComment);
        }
    }
}

void Tab_SetSyntaxHighlight(Tab* tab)
{
    tab->syntax = NULL;
    if (tab->filename == NULL)
        return;

    char* ext = strrchr(tab->filename, '.');

    for (unsigned int j = 0; syntaxDatabase[j].fileType != NULL; j++) {
        Syntax* syn = &syntaxDatabase[j];
        for (unsigned int i = 0; syn->fileMatch[i] != NULL; i++) {
            bool isExt = (syn->fileMatch[i][0] == '.');
            if ((isExt && ext && !strcmp(ext, syn->fileMatch[i]))
                || (!isExt && strstr(tab->filename, syn->fileMatch[i]))) {
                tab->syntax = syn;
                Tab_UpdateSyntax(tab);
                return;
            }
        }
    }
}
