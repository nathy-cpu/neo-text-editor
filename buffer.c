#include "buffer.h"
#include <bits/types/struct_iovec.h>

/******* screen buffer handling ********/

void ScreenBuffer_append(ScreenBuffer* sbuf, const char* string, size_t size)
{
    char* temp = realloc(sbuf->string, sbuf->size + size);
    if (temp == NULL)
        die("realloc");

    memcpy(&temp[sbuf->size], string, size);

    sbuf->string = temp;
    sbuf->size += size;
}

void ScreenBuffer_free(ScreenBuffer* sbuf)
{
    free(sbuf->string);
    sbuf->size = 0;
}

/******* syntax highlighting ********/

bool isSeparator(int character)
{
    return isspace(character) || character == '\0' || strchr(",.()+-/*=~%<>[];{}", character) != NULL;
}

char* getSyntaxColor(int highlight)
{
    switch (highlight)
    {
        case HIGHLIGHT_NUMBER:
            return "\x1b[0m\x1b[38:5:207m";
            break;

        case HIGHLIGHT_STRING:
            return "\x1b[0m\x1b[38:5:208m";
            break;

        case HIGHLIGHT_CHARACTER:
            return "\x1b[0m\x1b[38:5:178m";
            break;

        case HIGHLIGHT_COMMENT:
            return "\x1b[3m\x1b[38:5:70m";
            break;

        case HIGHLIGHT_KEYWORD:
            return "\x1b[1m\x1b[38:5:162m";
            break;

        case HIGHLIGHT_TYPE:
            return "\x1b[0m\x1b[38:5:33m";
            break;

        case HIGHLIGHT_SELECTION:
            return "\x1b[1m\x1b[48:5:240m\x1b[38:5:51m";
            break;

        case HIGHLIGHT_PREPROCESSOR:
            return "\x1b[0m\x1b[38:5:20m";
            break;


        case HIGHLIGHT_SEPARATOR:
            return "\x1b[1m\x1b[38:5:229m";
            break;

        case HIGHLIGHT_NORMAL:
            return "\x1b[0m\x1b[38:5:15m";

        default:
            return "\x1b[0m\x1b[37m";
            break;
    }
}

/******* text row operations ********/

void    TextRow_updateRender(TextRow* row)
{
    unsigned int tabs = 0;
    for (size_t i = 0; i < row->textSize; i++)
    {
        if (row->text[i] == '\t')
            tabs++;
    }

    free(row->render);
    char* temp = malloc(row->textSize + tabs * (TAB_STOP - 1) + 1);
    if (temp != NULL)
        row->render = temp;
    else
        die("malloc");

    size_t index = 0;
    for (size_t j = 0; j < row->textSize; j++)
    {
        if (row->text[j] == '\t')
        {
            row->render[index] = ' ';
            index++;
            while (index % TAB_STOP != 0)
            {
                row->render[index] = ' ';
                index++;
            }
        }
        else
        {
            row->render[index] = row->text[j];
            index++;
        }
    }

    row->render[index] = '\0';
    row->renderSize = index;
}

void    TextRow_updateSyntax(TextRow* row, const Syntax syn)
{
    unsigned char* temp = realloc(row->highlight, row->renderSize);

    if (temp == NULL)
        die("realloc");

    row->highlight = temp;

    memset(row->highlight, HIGHLIGHT_NORMAL, row->renderSize);

    if (syn.fileType == NULL)
        return;

    bool previousSeparator = true;
    bool inString = false;
    bool inComment = (row->index > 0 && (row - 1)->openComment);

    for (size_t i = 0; i < row->renderSize; i++)
    {
        unsigned char previousHighlight = (i > 0)
                                        ? row->highlight[i - 1]
                                        : HIGHLIGHT_NORMAL;


        if (previousWhiteSpace(row, i) && syn.preprocessorStart != NULL)
        {
            for (size_t k = 0; syn.preprocessorStart[k] != NULL; k++)
            {
                int len = strlen(syn.preprocessorStart[k]);

                if (!strncmp(&row->render[i], syn.preprocessorStart[k], len))
                {
                    memset(&row->highlight[i], HIGHLIGHT_PREPROCESSOR, row->renderSize - i);
                    return;
                }
            }
        }

        if (!inString && !inComment && !strncmp(&row->render[i], syn.singleLineCommentStarter, strlen(syn.singleLineCommentStarter)))
        {
            memset(&row->highlight[i], HIGHLIGHT_COMMENT, row->renderSize - i);
            break;
        }

        if (!inComment && !inString && !strncmp(&row->render[i], syn.multilineCommentStart, strlen(syn.multilineCommentStart)))
        {
            int len = strlen(syn.multilineCommentStart);

            memset(&row->highlight[i], HIGHLIGHT_COMMENT, len);
            i += len - 1;
            inComment = true;
            continue;
        }

        if (inComment && !inString)
        {
            row->highlight[i] = HIGHLIGHT_COMMENT;
            int len = strlen(syn.multilineCommentEnd);
            if (!strncmp(&row->render[i], syn.multilineCommentEnd, len))
            {
                memset(&row->highlight[i], HIGHLIGHT_COMMENT, len);
                i += len - 1;
                inComment = false;
                previousSeparator = true;
            }
            continue;
        }

        if (inString)
        {
            row->highlight[i] = HIGHLIGHT_STRING;
            previousSeparator = true;
            if (row->render[i] == '"')
                inString = false;
            continue;
        }
        else
        {
            if (row->render[i] == '"')
            {
                inString = true;
                row->highlight[i] = HIGHLIGHT_STRING;
                continue;
            }
        }


        if (isdigit(row->render[i]) && (previousSeparator || previousHighlight == HIGHLIGHT_NUMBER))
        {
            row->highlight[i] = HIGHLIGHT_NUMBER;
            previousSeparator = false;
            continue;
        }

        if (previousSeparator)
        {
            int j;
            for(j = 0; syn.keywords[j] != NULL; j++)
            {
                int keywordLength = strlen(syn.keywords[j]);

                if (!strncmp(&row->render[i], syn.keywords[j], keywordLength) && isSeparator(row->render[i + keywordLength]))
                {
                    memset(&row->highlight[i], HIGHLIGHT_KEYWORD, keywordLength);
                    i +=keywordLength - 1;
                    break;
                }
            }

            if (syn.keywords[j] != NULL)
            {
                previousSeparator = false;
                continue;
            }

            for (j = 0; syn.types[j] != NULL; j++)
            {
                int typesLength = strlen(syn.types[j]);

                if (!strncmp(&row->render[i], syn.types[j], typesLength) && isSeparator(row->render[i + typesLength]))
                {
                    memset(&row->highlight[i], HIGHLIGHT_TYPE, typesLength);
                    i += typesLength - 1;
                    break;
                }
            }

            if (syn.types[j] != NULL)
            {
                previousSeparator = false;
                continue;
            }
        }

        if (isSeparator(row->render[i]) && !isspace(row->render[i]))
        {
            row->highlight[i] = HIGHLIGHT_SEPARATOR;
            previousSeparator = true;
            continue;
        }

        previousSeparator = isSeparator(row->render[i]);
    }

    //bool isChanged = (row->openComment != inComment);
    row->openComment = inComment;
    //if (isChanged && (row + 1) != NULL && (row + 1)->index == row->index + 1)
    //    TextRow_updateSyntax(row + 1, syn);
}

void    TextRow_free(TextRow* row)
{
    free(row->render);
    free(row->text);
    free(row->highlight);
}

void    TextRow_insertChar(TextRow* row, size_t index, short int input, const Syntax syn)
{
    if (index > row->textSize)
        index = row->textSize;

    char* temp = realloc(row->text, row->textSize + 2);
    if (temp != NULL)
        row->text = temp;
    else
        die("realloc");

    memcpy(&row->text[index + 1], &row->text[index], row->textSize - index + 1);

    row->textSize++;
    row->text[index] = input;
    TextRow_updateRender(row);
    TextRow_updateSyntax(row, syn);
}

void    TextRow_appendString(TextRow* row, char* str, size_t size, const Syntax syn)
{
    char* temp = realloc(row->text, row->textSize + size + 1);
    if (temp != NULL)
        row->text = temp;
    else
        die("realloc");

    memcpy(&row->text[row->textSize], str, size);

    row->textSize += size;
    row->text[row->textSize] = '\0';
    TextRow_updateRender(row);
    TextRow_updateSyntax(row, syn);
}

void    TextRow_deleteChar(TextRow* row, size_t index, const Syntax syn)
{
    if (index >= row->textSize)
        return;

    memcpy(&row->text[index], &row->text[index + 1], row->textSize - index);

    row->textSize--;
    TextRow_updateRender(row);
    TextRow_updateSyntax(row, syn);
}

size_t  TextRow_getRenderX(TextRow* row, size_t cursorX)
{
    size_t rx = 0;
    for (size_t i = 0; i < cursorX && i < row->textSize; i++)
    {
        if (row->text[i] == '\t')
            rx += (TAB_STOP - 1) - (rx % TAB_STOP);
        rx++;
    }

    return rx;
}

size_t  TextRow_getCursorX(TextRow* row, size_t renderX)
{
    size_t cx, rx = 0;

    for (cx = 0; cx < row->textSize && rx <= renderX; cx++)
    {
        if (row->text[cx] == '\t')
            rx += (TAB_STOP - 1) - (rx % TAB_STOP);
        rx++;
    }
    return cx;
}

bool    previousWhiteSpace(const TextRow* row, size_t index)
{
    for (size_t i = 0; i < index; i++)
    {
        if (row->render[i] != ' ')
            return false;
    }
    return true;
}

/******* text buffer operations ********/

void    TextBuffer_insertTextRow(TextBuffer* tbuf, size_t index, const char* str, size_t size)
{
    if (index > tbuf->numberofTextRows)
        return;

    TextRow* temp = realloc(tbuf->textRow, sizeof(TextRow) * (tbuf->numberofTextRows + 1));
    if (temp != NULL)
        tbuf->textRow = temp;
    else
        die("realloc");

    memmove(&tbuf->textRow[index + 1], &tbuf->textRow[index], sizeof(TextRow) * (tbuf->numberofTextRows - index));

    for (size_t j = index + 1; j <= tbuf->numberofTextRows; j++)
        tbuf->textRow[j].index++;

    tbuf->textRow[index].index = index;

    tbuf->textRow[index].textSize = size;
    tbuf->textRow[index].text = malloc(size + 1);
    memcpy(tbuf->textRow[index].text, str, size);
    tbuf->textRow[index].text[size] = '\0';

    tbuf->textRow[index].renderSize = 0;
    tbuf->textRow[index].render = NULL;
    tbuf->textRow[index].highlight = NULL;
    tbuf->textRow[index].openComment = false;

    TextRow_updateRender(&tbuf->textRow[index]);
    //TextRow_updateSyntax(&tbuf->textRow[index], tbuf->syntax);
    TextBuffer_updateSyntax(tbuf, index);

    tbuf->numberofTextRows++;
}

void    TextBuffer_deleteTextRow(TextBuffer* tbuf, size_t index)
{
    if (index >= tbuf->numberofTextRows)
        return;

    TextRow_free(&tbuf->textRow[index]);

    memmove(&tbuf->textRow[index], &tbuf->textRow[index + 1], sizeof(TextRow) * (tbuf->numberofTextRows - index - 1));

    for (size_t j = index; j < tbuf->numberofTextRows; j++)
        tbuf->textRow[j].index--;

    tbuf->numberofTextRows--;

    TextBuffer_updateSyntax(tbuf, index);
}

void    TextBuffer_updateSyntax(TextBuffer* tbuf, size_t offset)
{
    for (size_t i = offset; i < tbuf->numberofTextRows; i++)
        TextRow_updateSyntax(&tbuf->textRow[i], tbuf->syntax);
}

char*   TextBuffer_toString(TextBuffer* tbuf, size_t* bufferSize)
{
    size_t totalSize = 0;
    for (size_t i = 0; i < tbuf->numberofTextRows; i++)
        totalSize += tbuf->textRow[i].textSize + 1;
    *bufferSize = totalSize;

    char* buffer;
    if((buffer = malloc(totalSize)) == NULL)
        die("malloc");

    for (size_t i = 0; i < tbuf->numberofTextRows; i++)
    {
        memcpy(buffer, tbuf->textRow[i].text, tbuf->textRow[i].textSize);

        buffer += tbuf->textRow[i].textSize;
        *buffer = '\n';
        buffer++;
    }

    return buffer;
}
