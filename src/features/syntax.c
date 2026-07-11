#include "../neo.h"
#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

char* GetSyntaxColor(const Config* config, HighlightType highlightType)
{
    if (config && highlightType >= 0 && highlightType < 9 && config->syntaxColors[highlightType]) {
        return config->syntaxColors[highlightType];
    }
    switch (highlightType) {
    case HIGHLIGHT_NORMAL:
        return NULL;
    case HIGHLIGHT_NUMBER:
        return "35"; // purple/magenta
    case HIGHLIGHT_MATCH:
        return "34"; // blue
    case HIGHLIGHT_STRING:
        return "38;5;208"; // orange
    case HIGHLIGHT_CHARACTER:
        return "33"; // yellow
    case HIGHLIGHT_COMMENT:
        return "90"; // dim gray
    case HIGHLIGHT_KEYWORD:
        return "34"; // blue
    case HIGHLIGHT_TYPE:
        return "32"; // green
    case HIGHLIGHT_SYMBOL:
        return "36"; // cyan (for punctuation)
    default:
        return "39";
    }
}

static bool IsSeparator(int c) { return isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>[];{}!&|^?:", c) != NULL; }

static void UpdateLineSyntax(Line* line, Syntax* syntax, bool* inMultiLineComment)
{
    Array_Clear(&line->styles);

    Slice textSlice = Line_GetText(line);
    size_t length = Line_Length(line);
    char* text = (char*)textSlice.data;

    // Default to HIGHLIGHT_NORMAL
    for (size_t i = 0; i < length; i++) {
        char val = HIGHLIGHT_NORMAL;
        Array_Append(&line->styles, &val, 1);
    }

    Slice stylesSlice = Array_ToSlice(&line->styles);
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
                    styles[i] = inSingleString ? HIGHLIGHT_CHARACTER : HIGHLIGHT_STRING;
                    inString = 0;
                    i++;
                    continue;
                } else if (!inString) {
                    inString = 1;
                    inSingleString = (c == '\'');
                    styles[i] = inSingleString ? HIGHLIGHT_CHARACTER : HIGHLIGHT_STRING;
                    i++;
                    continue;
                }
            }
        }

        if (inString) {
            styles[i] = inSingleString ? HIGHLIGHT_CHARACTER : HIGHLIGHT_STRING;
            if (c == '\\' && i + 1 < length) { // Escape sequence
                styles[i + 1] = inSingleString ? HIGHLIGHT_CHARACTER : HIGHLIGHT_STRING;
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
        if (previousSeparator && !isspace(c) && c != '\0' && styles[i] == HIGHLIGHT_NORMAL) {
            styles[i] = HIGHLIGHT_SYMBOL;
        }
        i++;
    }
}

void Tab_UpdateSyntax(Tab* tab)
{
    if (!tab->config || !tab->config->syntaxEnabled || tab->syntax == NULL) {
        // Clear syntax styles for all lines
        for (size_t i = 0; i < Buffer_GetLineCount(tab->buffer); i++) {
            Line* line = Buffer_GetLine(tab->buffer, i);
            if (line) {
                Array_Clear(&line->styles);
                size_t length = Line_Length(line);
                for (size_t j = 0; j < length; j++) {
                    char val = HIGHLIGHT_NORMAL;
                    Array_Append(&line->styles, &val, 1);
                }
            }
        }
        return;
    }

    LOG_DEBUG("Tab_UpdateSyntax: updating syntax highlighting for tab '%s' using '%s'", tab->filename ? tab->filename : "<scratch>", tab->syntax->fileType);

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
    if (!tab->config || !tab->config->syntaxEnabled || tab->filename == NULL)
        return;

    char* ext = strrchr(tab->filename, '.');

    for (size_t j = 0; j < Array_Size(&tab->config->syntaxDatabase); j++) {
        Syntax* syn = (Syntax*)Array_At(&tab->config->syntaxDatabase, j);
        for (unsigned int i = 0; syn->fileMatch[i] != NULL; i++) {
            bool isExt = (syn->fileMatch[i][0] == '.');
            if ((isExt && ext && !strcmp(ext, syn->fileMatch[i]))
                || (!isExt && strstr(tab->filename, syn->fileMatch[i]))) {
                tab->syntax = syn;
                LOG_INFO("Tab_SetSyntaxHighlight: detected '%s' syntax for file '%s'", syn->fileType, tab->filename);
                Tab_UpdateSyntax(tab);
                return;
            }
        }
    }
    LOG_INFO("Tab_SetSyntaxHighlight: no matching syntax rules found for file '%s'", tab->filename);
}
