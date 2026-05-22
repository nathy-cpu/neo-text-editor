#include "../neo.h"
#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

char* CfileExtensions[] = { ".c", ".h", NULL };
char* CppfileExtensions[] = { ".cpp", ".hpp", NULL };

char* Ckeywords[] = { "alignas", "alignof", "auto", "break", "case", "char", "const", "constexpr", "continue",
    "default", "do", "double", "else", "enum", "extern", "false", "float", "for", "goto", "if", "inline", "nullptr",
    "register", "restrict", "return", "sizeof", "static", "static_assert", "struct", "switch", "thread_local", "true",
    "typedef", "typeof", "typeof_unqual", "union", "void", "volatile", "while", "size_t", "ssize_t", NULL };

char* Ctypes[] = { "int", "long", "short", "double", "float", "char", "unsigned", "signed", "void", "bool", NULL };

char* Cppkeywords[] = { "class", "delete", "new", "namespace", "try", "catch", "throw", "public", "private",
    "protected", "virtual", "template", "typename", NULL };

char* Cpptypes[] = { "bool", "char8_t", "char16_t", "char32_t", "wchar_t", NULL };

Syntax HLDB[] = { { "C", CfileExtensions, Ckeywords, Ctypes, "//", "/*", "*/" },
    { "C++", CppfileExtensions, Cppkeywords, Cpptypes, "//", "/*", "*/" },
    { NULL, NULL, NULL, NULL, NULL, NULL, NULL } };

char* GetSyntaxColor(int highlight)
{
    switch (highlight) {
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

static bool is_separator(int c) { return isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>[];", c) != NULL; }

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

    int scs_len = syntax->singleLineCommentStart ? strlen(syntax->singleLineCommentStart) : 0;
    int mcs_len = syntax->multiLineCommentStart ? strlen(syntax->multiLineCommentStart) : 0;
    int mce_len = syntax->multiLineCommentEnd ? strlen(syntax->multiLineCommentEnd) : 0;

    bool prev_sep = true;
    int in_string = 0;
    bool in_single_string = false;

    size_t i = 0;
    while (i < length) {
        char c = text[i];

        // Strings
        if (!(*inMultiLineComment)) {
            if (c == '"' || c == '\'') {
                if (in_string && (in_single_string ? c == '\'' : c == '"')) {
                    styles[i] = HIGHLIGHT_STRING;
                    in_string = 0;
                    i++;
                    continue;
                } else if (!in_string) {
                    in_string = 1;
                    in_single_string = (c == '\'');
                    styles[i] = HIGHLIGHT_STRING;
                    i++;
                    continue;
                }
            }
        }

        if (in_string) {
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
        if (scs_len && !(*inMultiLineComment)) {
            if (!strncmp(&text[i], syntax->singleLineCommentStart, scs_len)) {
                memset(&styles[i], HIGHLIGHT_COMMENT, length - i);
                break;
            }
        }

        // Multi-line comment
        if (mcs_len && mce_len) {
            if (*inMultiLineComment) {
                styles[i] = HIGHLIGHT_COMMENT;
                if (!strncmp(&text[i], syntax->multiLineCommentEnd, mce_len)) {
                    memset(&styles[i], HIGHLIGHT_COMMENT, mce_len);
                    i += mce_len;
                    *inMultiLineComment = false;
                    prev_sep = true;
                    continue;
                }
                i++;
                continue;
            } else if (!strncmp(&text[i], syntax->multiLineCommentStart, mcs_len)) {
                memset(&styles[i], HIGHLIGHT_COMMENT, mcs_len);
                i += mcs_len;
                *inMultiLineComment = true;
                continue;
            }
        }

        // Numbers
        if ((isdigit(c) && (prev_sep || (i > 0 && styles[i - 1] == HIGHLIGHT_NUMBER)))
            || (c == '.' && i > 0 && styles[i - 1] == HIGHLIGHT_NUMBER)) {
            styles[i] = HIGHLIGHT_NUMBER;
            prev_sep = false;
            i++;
            continue;
        }

        if (syntax->keywords || syntax->types) {
            if (prev_sep) {
                bool matched = false;
                if (syntax->keywords) {
                    for (int j = 0; syntax->keywords[j]; j++) {
                        int klen = strlen(syntax->keywords[j]);
                        if (!strncmp(&text[i], syntax->keywords[j], klen)
                            && (i + klen == length || is_separator(text[i + klen]))) {
                            memset(&styles[i], HIGHLIGHT_KEYWORD, klen);
                            i += klen;
                            matched = true;
                            break;
                        }
                    }
                }
                if (!matched && syntax->types) {
                    for (int j = 0; syntax->types[j]; j++) {
                        int klen = strlen(syntax->types[j]);
                        if (!strncmp(&text[i], syntax->types[j], klen)
                            && (i + klen == length || is_separator(text[i + klen]))) {
                            memset(&styles[i], HIGHLIGHT_TYPE, klen);
                            i += klen;
                            matched = true;
                            break;
                        }
                    }
                }
                if (matched) {
                    prev_sep = false;
                    continue;
                }
            }
        }

        prev_sep = is_separator(c);
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

    for (unsigned int j = 0; HLDB[j].fileType != NULL; j++) {
        Syntax* syn = &HLDB[j];
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
