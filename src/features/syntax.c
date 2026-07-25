#include "syntax.h"
#include "../core/buffer.h"
#include "../utils/logger.h"
#include "../utils/utf8.h"
#include "tab.h"
#include <assert.h>
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

static bool IsSeparator(char c)
{
    return ByteIsSpace(c) || c == '\0' || strchr(",.()+-/*=~%<>[];{}!&|^?:", (unsigned char)c) != NULL;
}

// Bounded token lookahead: never reads past `length`, which also guarantees
// any following memset over [pos, pos + needleLength) stays inside the styles
// array (sized to `length`). The line text is a Slice into a shared scratch
// buffer with NO terminator and stale bytes beyond `length` -- an unbounded
// strncmp both overreads and false-matches on those stale bytes.
static bool MatchAt(const char* text, size_t length, size_t pos, const char* needle, size_t needleLength)
{
    return needleLength > 0 && pos + needleLength <= length && memcmp(text + pos, needle, needleLength) == 0;
}

static void UpdateLineSyntax(Line* line, struct Buffer* buffer, Syntax* syntax, bool* inMultiLineComment)
{
    size_t length = line->length;
    if (length == 0)
        return;
    Slice textSlice = Line_GetText(line, buffer);
    char* text = (char*)textSlice.data;

    // Lines belonging to a tab with no active syntax are never populated at all
    // (see Tab_UpdateSyntax), so this is the first time this line's styles array
    // is touched -- allocate it lazily, only once real highlighting is needed.
    if (!line->styles) {
        line->styles = calloc(1, sizeof(Array));
        assert(line->styles != NULL);
        Array_InitChar(line->styles, length > 0 ? length : 1);
    }
    Array_Clear(line->styles);

    // Default to HIGHLIGHT_NORMAL
    for (size_t i = 0; i < length; i++) {
        char val = HIGHLIGHT_NORMAL;
        Array_Append(line->styles, &val, 1);
    }

    Slice stylesSlice = Array_ToSlice(line->styles);
    char* styles = (char*)stylesSlice.data;

    if (syntax == NULL)
        return;

    size_t singleCommentLength = syntax->singleLineCommentStart ? strlen(syntax->singleLineCommentStart) : 0;
    size_t multiCommentStartLength = syntax->multiLineCommentStart ? strlen(syntax->multiLineCommentStart) : 0;
    size_t multiCommentEndLength = syntax->multiLineCommentEnd ? strlen(syntax->multiLineCommentEnd) : 0;

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
                    previousSeparator = false; // a closing quote is not a separator
                    i++;
                    continue;
                } else if (!inString) {
                    inString = 1;
                    inSingleString = (c == '\'');
                    styles[i] = inSingleString ? HIGHLIGHT_CHARACTER : HIGHLIGHT_STRING;
                    previousSeparator = false;
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

        // Inside a multi-line comment: only its end marker matters here.
        if (multiCommentStartLength && multiCommentEndLength && *inMultiLineComment) {
            styles[i] = HIGHLIGHT_COMMENT;
            if (MatchAt(text, length, i, syntax->multiLineCommentEnd, multiCommentEndLength)) {
                memset(&styles[i], HIGHLIGHT_COMMENT, multiCommentEndLength);
                i += multiCommentEndLength;
                *inMultiLineComment = false;
                previousSeparator = true;
                continue;
            }
            i++;
            continue;
        }

        // Multi-line comment start is checked BEFORE the single-line marker:
        // for languages where the single-line marker prefixes the multi-line
        // one (Lua: "--" vs "--[["), the longer match must win or block
        // comments never open.
        if (multiCommentStartLength && multiCommentEndLength
            && MatchAt(text, length, i, syntax->multiLineCommentStart, multiCommentStartLength)) {
            memset(&styles[i], HIGHLIGHT_COMMENT, multiCommentStartLength);
            i += multiCommentStartLength;
            *inMultiLineComment = true;
            continue;
        }

        // Single line comment
        if (singleCommentLength && MatchAt(text, length, i, syntax->singleLineCommentStart, singleCommentLength)) {
            memset(&styles[i], HIGHLIGHT_COMMENT, length - i);
            break;
        }

        // Numbers
        if ((ByteIsDigit(c) && (previousSeparator || (i > 0 && styles[i - 1] == HIGHLIGHT_NUMBER)))
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
                        size_t keywordLength = strlen(syntax->keywords[j]);
                        if (MatchAt(text, length, i, syntax->keywords[j], keywordLength)
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
                        size_t keywordLength = strlen(syntax->types[j]);
                        if (MatchAt(text, length, i, syntax->types[j], keywordLength)
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
        if (previousSeparator && !ByteIsSpace(c) && c != '\0' && styles[i] == HIGHLIGHT_NORMAL) {
            styles[i] = HIGHLIGHT_SYMBOL;
        }
        i++;
    }
}

void Tab_UpdateSyntax(Tab* tab, size_t maxLine)
{
    if (!tab->config || !tab->config->syntaxEnabled || tab->syntax == NULL) {
        if (tab->buffer) {
            tab->buffer->syntaxDirtyLineStart = SIZE_MAX;
        }
        return;
    }

    size_t dirtyStart = tab->buffer->syntaxDirtyLineStart;
    size_t lineCount = Buffer_GetLineCount(tab->buffer);
    bool hasPendingEdit = dirtyStart != SIZE_MAX && dirtyStart < lineCount;

    if (!hasPendingEdit && maxLine <= tab->syntaxHighWaterMark) {
        // Clear syntaxDirtyLineStart just in case
        tab->buffer->syntaxDirtyLineStart = SIZE_MAX;
        return;
    }

    LOG_DEBUG("Tab_UpdateSyntax: updating syntax highlighting for tab '%s' using '%s'",
        tab->filename ? tab->filename : "<scratch>", tab->syntax->fileType);

    // Never leave a gap between what's already covered and where we resume:
    // a pending edit inside already-covered territory restarts there, but one
    // beyond the high-water mark (shouldn't normally happen -- editing a line
    // requires it to be visible, which requires prior coverage there) still
    // resumes from the mark rather than skipping ahead to the edit.
    size_t startLine
        = (hasPendingEdit && dirtyStart < tab->syntaxHighWaterMark) ? dirtyStart : tab->syntaxHighWaterMark;
    if (startLine >= lineCount) {
        tab->buffer->syntaxDirtyLineStart = SIZE_MAX;
        return;
    }

    size_t endLine = maxLine > tab->syntaxHighWaterMark ? maxLine : tab->syntaxHighWaterMark;
    if (endLine > lineCount) {
        endLine = lineCount;
    }
    if (endLine <= startLine) {
        endLine = startLine + 1;
    }

    bool inMultiLineComment = false;
    if (startLine > 0) {
        Line* prevLine = Buffer_GetLine(tab->buffer, startLine - 1);
        if (prevLine && prevLine->commentStateOutValid) {
            inMultiLineComment = prevLine->commentStateOut;
        }
    }

    size_t highestTouched = startLine;
    for (size_t i = startLine; i < endLine; i++) {
        Line* line = Buffer_GetLine(tab->buffer, i);
        if (!line)
            continue;

        bool oldStateValid = line->commentStateOutValid;
        bool oldState = line->commentStateOut;

        if (line->length > LINE_HUGE_THRESHOLD) {
            // Too large to highlight cheaply -- leave styles unallocated; the
            // renderer already treats that as all-normal. We can't scan it to
            // know whether it opens/closes a multi-line comment, so carry the
            // incoming state through unchanged (a documented, cosmetic-only
            // trade-off for huge lines, same spirit as the lazy high-water-mark
            // jump case above).
            line->commentStateOutValid = false;
            highestTouched = i + 1;
            continue;
        }

        UpdateLineSyntax(line, tab->buffer, tab->syntax, &inMultiLineComment);

        line->commentStateOut = inMultiLineComment;
        line->commentStateOutValid = true;
        highestTouched = i + 1;

        // Lines beyond the edit whose exit state didn't change are unaffected downstream.
        if (i > startLine && oldStateValid && oldState == inMultiLineComment) {
            break;
        }
    }

    if (highestTouched > tab->syntaxHighWaterMark) {
        tab->syntaxHighWaterMark = highestTouched;
    }

    tab->buffer->syntaxDirtyLineStart = SIZE_MAX;
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
                // Actual highlighting is deferred to the render/scroll path
                // (Tab_UpdateSyntax), which extends coverage lazily instead
                // of highlighting the whole file synchronously here.
                return;
            }
        }
    }
    LOG_INFO("Tab_SetSyntaxHighlight: no matching syntax rules found for file '%s'", tab->filename);
}
