#include "buffer.h"

/******* screen buffer handling ********/

void ScreenBufferAppend(ScreenBuffer* sbuf, const char* string, size_t size)
{
    char* temp = realloc(sbuf->string, sbuf->size + size);
    if (temp == NULL)
        die("realloc");

    memcpy(&temp[sbuf->size], string, size);

    sbuf->string = temp;
    sbuf->size += size;
}

void ScreenBufferFree(ScreenBuffer* sbuf)
{
    free(sbuf->string);
    sbuf->size = 0;
}

/******* syntax highlighting ********/

bool isSeparator(int character)
{
    return isspace(character) || character == '\0' || strchr(",.()+-/*=~%<>[];", character) != NULL;
}

char* getSyntaxColor(int highlight)
{
    switch (highlight)
    {
        case HIGHLIGHT_NUMBER:
            return "\x1b[38:5:207m";
            break;

        case HIGHLIGHT_STRING:
            return "\x1b[38:5:208m";
            break;

        case HIGHLIGHT_CHARACTER:
            return "\x1b[38:5:178m";
            break;

        case HIGHLIGHT_COMMENT:
            return "\x1b[3m\x1b[38:5:70m";
            break;

        case HIGHLIGHT_KEYWORD:
            return "\x1b[1m\x1b[38:5:162m";
            break;

        case HIGHLIGHT_TYPE:
            return "\x1b[38:5:33m";
            break;

        case HIGHLIGHT_SELECTION:
            return "\x1b[48:5:240m\x1b[38:5:51m";
            break;

        case HIGHLIGHT_PREPROCESSOR:
            return "\x1b[38:5:20m";

        default:
            return "\x1b[0m\x1b[37m";
            break;
    }
}

/******* text row operations ********/

void    TextRowUpdateRender(TextRow* row)
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

void    TextRowUpdateSyntax(TextRow* row, const Syntax syn)
{
    unsigned char* temp = realloc(row->highlight, row->renderSize);
    if (temp == NULL)
        die("realloc");
    else
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

        if (i == 0 && !strncmp(&row->render[i], syn.preprocessorStart, strlen(syn.preprocessorStart)))
        {
            memset(&row->highlight[i], HIGHLIGHT_PREPROCESSOR, row->renderSize);
            break;
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

            int k;
            for (k = 0; syn.types[k] != NULL; k++)
            {
                int typesLength = strlen(syn.types[k]);

                if (!strncmp(&row->render[i], syn.types[k], typesLength) && isSeparator(row->render[i + typesLength]))
                {
                    memset(&row->highlight[i], HIGHLIGHT_TYPE, typesLength);
                    i += typesLength - 1;
                    break;
                }
            }

            if (syn.types[k] != NULL)
            {
                previousSeparator = false;
                continue;
            }
        }

        previousSeparator = isSeparator(row->render[i]);
    }

    bool isChanged = (row->openComment != inComment);
    row->openComment = inComment;
    if (isChanged && (row + 1) != NULL && (row + 1)->index == row->index + 1)
        TextRowUpdateSyntax(row + 1, syn);
}

void    TextRowFree(TextRow* row)
{
    free(row->render);
    free(row->text);
    free(row->highlight);
}

void    TextRowInsertChar(TextRow* row, size_t index, short int input, const Syntax syn)
{
    if (index > row->textSize)
        index = row->textSize;

    char* temp = realloc(row->text, row->textSize + 2);
    if (temp != NULL)
        row->text = temp;
    else
        die("realloc");

    memmove(&row->text[index + 1], &row->text[index], row->textSize - index + 1);

    row->textSize++;
    row->text[index] = input;
    TextRowUpdateRender(row);
    TextRowUpdateSyntax(row, syn);
}

void    TextRowAppendString(TextRow* row, char* str, size_t size, const Syntax syn)
{
    char* temp = realloc(row->text, row->textSize + size + 1);
    if (temp != NULL)
        row->text = temp;
    else
        die("realloc");

    memcpy(&row->text[row->textSize], str, size);

    row->textSize += size;
    row->text[row->textSize] = '\0';
    TextRowUpdateRender(row);
    TextRowUpdateSyntax(row, syn);
}

void    TextRowDeleteChar(TextRow* row, size_t index, const Syntax syn)
{
    if (index >= row->textSize)
        return;

    memmove(&row->text[index], &row->text[index + 1], row->textSize - index);

    row->textSize--;
    TextRowUpdateRender(row);
    TextRowUpdateSyntax(row, syn);
}

size_t  TextRowGetRenderX(TextRow* row, size_t cursorX)
{
    size_t rx = 0;
    for (size_t i = 0; i < cursorX; i++)
    {
        if (row->text[i] == '\t')
            rx += (TAB_STOP - 1) - (rx % TAB_STOP);
        rx++;
    }

    return rx;
}

size_t  TextRowGetCursorX(TextRow* row, size_t renderX)
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

/******* text buffer operations ********/

void    TextBufferInsertTextRow(TextBuffer* tbuf, size_t index, const char* str, size_t size)
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

    TextRowUpdateRender(&tbuf->textRow[index]);
    TextRowUpdateSyntax(&tbuf->textRow[index], tbuf->syntax);

    tbuf->numberofTextRows++;
}

void    TextBufferDeleteTextRow(TextBuffer* tbuf, size_t index)
{
    if (index >= tbuf->numberofTextRows)
        return;

    TextRowFree(&tbuf->textRow[index]);

    memmove(&tbuf->textRow[index], &tbuf->textRow[index + 1], sizeof(TextRow) * (tbuf->numberofTextRows - index - 1));

    for (size_t j = index; j < tbuf->numberofTextRows; j++)
        tbuf->textRow[j].index--;

    tbuf->numberofTextRows--;
}

char*   TextBufferToString(TextBuffer* tbuf, size_t* bufferSize)
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
