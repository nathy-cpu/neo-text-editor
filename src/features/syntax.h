#pragma once

#include "config.h"

typedef enum {
    HIGHLIGHT_NORMAL = 0,
    HIGHLIGHT_NUMBER,
    HIGHLIGHT_MATCH,
    HIGHLIGHT_STRING,
    HIGHLIGHT_CHARACTER,
    HIGHLIGHT_COMMENT,
    HIGHLIGHT_KEYWORD,
    HIGHLIGHT_TYPE,
    HIGHLIGHT_SYMBOL
} HighlightType;

/**
 * @brief Retrieves the ANSI color code for a specific highlight type.
 */
char* GetSyntaxColor(const Config* config, HighlightType highlightType);
