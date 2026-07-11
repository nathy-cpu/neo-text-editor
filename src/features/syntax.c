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
    Slice textSlice = Line_GetText(line);
    size_t length = Line_Length(line);
    char* text = (char*)textSlice.data;

    // Lines belonging to a tab with no active syntax are never populated at all
    // (see Tab_UpdateSyntax), so this is the first time this line's styles array
    // is touched -- allocate it lazily, only once real highlighting is needed.
    if (!line->styles.data) {
        Array_InitChar(&line->styles, length > 0 ? length : 1);
    }
    Array_Clear(&line->styles);

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
        // Neither syntaxEnabled nor tab->syntax can change at runtime once this tab
        // exists (both are decided once, at load time, in Tab_SetSyntaxHighlight),
        // so a tab that reaches here has never had real highlight data written into
        // any line's styles -- there is nothing to clear. Lines' styles arrays are
        // left untouched (never even allocated, see UpdateLineSyntax), and the
        // renderer already treats an empty/undersized styles array as all-normal.
        return;
    }

    LOG_DEBUG("Tab_UpdateSyntax: updating syntax highlighting for tab '%s' using '%s'",
        tab->filename ? tab->filename : "<scratch>", tab->syntax->fileType);

    // Peek the pending edit's dirty line before any Buffer_GetLine/GetLineCount call
    // triggers a line cache rebuild and clears it.
    size_t startLine = Buffer_PeekDirtyLineStart(tab->buffer);
    size_t lineCount = Buffer_GetLineCount(tab->buffer);
    if (startLine == SIZE_MAX || startLine >= lineCount) {
        startLine = 0;
    }

    bool inMultiLineComment = false;
    if (startLine > 0) {
        Line* prevLine = Buffer_GetLine(tab->buffer, startLine - 1);
        if (prevLine && prevLine->commentStateOutValid) {
            inMultiLineComment = prevLine->commentStateOut;
        }
    }

    for (size_t i = startLine; i < lineCount; i++) {
        Line* line = Buffer_GetLine(tab->buffer, i);
        if (!line)
            continue;

        bool oldStateValid = line->commentStateOutValid;
        bool oldState = line->commentStateOut;

        UpdateLineSyntax(line, tab->syntax, &inMultiLineComment);

        line->commentStateOut = inMultiLineComment;
        line->commentStateOutValid = true;

        // Lines beyond the edit whose exit state didn't change are unaffected downstream.
        if (i > startLine && oldStateValid && oldState == inMultiLineComment) {
            break;
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
