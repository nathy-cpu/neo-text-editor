#include "editor.h"

/******* initializing the Editor ********/

void    disableRawMode(EditorConfiguration *config)
{
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &config->originalTermios) == -1)
        die("tcsetattr");

    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
}

void    enableRawMode(EditorConfiguration *config)
{
    if (tcgetattr(STDIN_FILENO, &config->originalTermios) == -1)
        die("tcgetattr");

    struct termios raw = config->originalTermios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
        die("tcsetattr");
}

void    EditorKill(EditorConfiguration *config)
{
    free(config->filename);
    for (size_t i = 0; i < config->textBuffer.numberofTextRows; i++)
    {
        free(config->textBuffer.textRow[i].text);
        free(config->textBuffer.textRow[i].render);
        free(config->textBuffer.textRow[i].highlight);
    }
    free(config->textBuffer.textRow);
    disableRawMode(config);
    // todo: figure out disableRawMode situation
}

int     EditorGetCursorPosition(EditorConfiguration *config)
{
    char buffer[32];
    unsigned int i = 0;

    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
        return -1;

    while (i < sizeof(buffer) - 1)
    {
        if (read(STDIN_FILENO, &buffer[i], 1) != 1)
            break;
        if (buffer[i] == 'R')
            break;
        i++;
    }
    buffer[i] = '\0';

    if (buffer[0] != '\x1b' || buffer[1] != '[')
        return -1;
    if (sscanf(&buffer[2], "%hu;%hu", &config->screenRows, &config->screenColumns) != 2)
        return -1;

    return 0;
}

int     EditorGetWindowSize(EditorConfiguration *config)
{
    struct winsize window;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &window) == -1 || window.ws_col == 0)
    {
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
            return -1;
        return EditorGetCursorPosition(config);
    }
    else
    {
        config->screenColumns = window.ws_col;
        config->screenRows = window.ws_row;
        return 0;
    }
}

void    EditorInit(EditorConfiguration *config)
{
    enableRawMode(config);
    config->cursorX = 0;
    config->cursorY = 0;
    config->renderX = 0;
    config->textBuffer.numberofTextRows = 0;
    config->textBuffer.textRow = NULL;
    config->textBuffer.syntax = NULL;
    config->rowOffset = 0;
    config->columnOffset = 0;
    config->filename = NULL;
    config->statusMessage[0] = '\0';
    config->statusMessageTime = 0;
    config->isSaved = true;

    if (EditorGetWindowSize(config) == -1)
        die("EditorGetWindowSize");

    config->screenRows -= 2;
}

void    EditorSetSyntaxHighlight(EditorConfiguration *config, Syntax HLDB[])
{
    config->textBuffer.syntax = NULL;
    if (config->filename == NULL)
        return;

    char* extention = strrchr(config->filename, '.');

    for (unsigned int j = 0; HLDB[j].fileType != NULL; j++)
    {
        Syntax* syn = &HLDB[j];

        for (unsigned int i = 0; syn->fileMatch[i] != 0; i++)
        {
            bool isExtension = (syn->fileMatch[i][0] == '.');
            if ((isExtension && extention && !strcmp(extention, syn->fileMatch[i])) || (!isExtension && strstr(config->filename, syn->fileMatch[i])))
            {
                config->textBuffer.syntax = syn;

                for(size_t k = 0; k < config->textBuffer.numberofTextRows; k++)
                    TextRowUpdateSyntax(&config->textBuffer.textRow[k], config->textBuffer.syntax);

                return;
            }
        }
    }
}

/******* handling files to edit ********/

void    EditorOpenFile(EditorConfiguration *config, const char* filename, Syntax HLDB[])
{
    free(config->filename);

    char* temp = strdup(filename);
    if (temp != NULL)
        config->filename = temp;
    else
        die("strdup");

    EditorSetSyntaxHighlight(config, HLDB);


    FILE* file = fopen(filename, "r");
    if (file == NULL)
        die("fopen");

    char* line = NULL;
    size_t linecap = 0;
    ssize_t linelen;

    while ((linelen = getline(&line, &linecap, file)) != -1)
    {
        while (linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r'))
            linelen--;

        TextBufferInsertTextRow(&config->textBuffer, config->textBuffer.numberofTextRows, line, linelen);
    }

    free(line);
    fclose(file);
    config->isSaved = true;
}

void    EditorSaveToFile(EditorConfiguration *config, Syntax HLDB[])
{
    if (config->filename == NULL)
    {
        config->filename = EditorPromptForInput(config, "Save file as: %s", NULL);
        if (config->filename ==  NULL)
        {
            EditorSetStatusMessage(config, "Save cancelled!");
            return;
        }

        EditorSetSyntaxHighlight(config, HLDB);
    }

    size_t bufferSize;
    char* buffer = TextBufferToString(&config->textBuffer, &bufferSize);

    int file = open(config->filename, O_RDWR | O_CREAT, 0644);

    if (file != -1)
    {
        if (ftruncate(file, bufferSize) != -1)
        {
            if (write(file, buffer, bufferSize) != -1)
            {
                close(file);
                free(buffer);
                config->isSaved = true;
                EditorSetStatusMessage(config, "%ldB written to disk.", bufferSize);
                return;
            }
        }
        close(file);
    }

    free(buffer);
    EditorSetStatusMessage(config, "Save Failed! Error: %s", strerror(errno)); // for testing only
}

/******* Editor output ********/

void    EditorScroll(EditorConfiguration *config)
{
    config->renderX = 0;
    if (config->cursorY < config->textBuffer.numberofTextRows)
        config->renderX = TextRowGetRenderX(&config->textBuffer.textRow[config->cursorY], config->cursorX);

    if (config->cursorY < config->rowOffset)
        config->rowOffset = config->cursorY;

    if (config->cursorY >= config->rowOffset + config->screenRows)
        config->rowOffset = config->cursorY - config->screenRows + 1;

    if (config->renderX < config->columnOffset)
        config->columnOffset = config->renderX;

    if (config->renderX >= config->columnOffset + config->screenColumns)
        config->columnOffset = config->renderX - config->screenColumns + 1;
}

void    EditorSetStatusMessage(EditorConfiguration *config, const char* fstring, ...)
{
    va_list arg;
    va_start(arg, fstring);
    vsnprintf(config->statusMessage, sizeof(config->statusMessage), fstring, arg);
    va_end(arg);
    config->statusMessageTime = time(NULL);
}

void    EditorDrawRows(EditorConfiguration *config, ScreenBuffer* sbuf)
{
    for (int i = 0; i < config->screenRows; i++)
    {
        size_t fileRow = i + config->rowOffset;
        if (fileRow >= config->textBuffer.numberofTextRows)
        {
            if (config->textBuffer.numberofTextRows == 0 && i == config->screenRows / 3)
            {
                char welcome[50];
                int welcomelen = snprintf(welcome, sizeof(welcome), "Neo Text Editor -- version %s", NEO_VERSION);

                if (welcomelen > config->screenColumns)
                    welcomelen = config->screenColumns;

                int padding = (config->screenColumns - welcomelen) / 2;
                if (padding > 0)
                {
                    ScreenBufferAppend(sbuf, ">", 1);
                    padding--;
                }
                while (padding)
                {
                    ScreenBufferAppend(sbuf, " ", 1);
                    padding--;
                }

                ScreenBufferAppend(sbuf, welcome, welcomelen);
            }
            else
                ScreenBufferAppend(sbuf, "~", 1);
        }
        else
        {
            ssize_t len = config->textBuffer.textRow[fileRow].renderSize - config->columnOffset;
            if (len < 0)
                len = 0;

            if (len > config->screenColumns)
                len = config->screenColumns;

            char* temp = &config->textBuffer.textRow[fileRow].render[config->columnOffset];
            unsigned char* highlight = &config->textBuffer.textRow[fileRow].highlight[config->columnOffset];
            char* currentColor = NULL;

            for (size_t j = 0; j < len; j++)
            {
                if (iscntrl(temp[j]))
                {
                    char symbol = (temp[j] <= 26) ? '@' + temp[j] : '?';
                    ScreenBufferAppend(sbuf, "\x1b[7m", 4);
                    ScreenBufferAppend(sbuf, &symbol, 1);
                    ScreenBufferAppend(sbuf, "\x1b[m", 3);
                    if (currentColor != NULL)
                    {
                        char buffer[30];
                        int len = snprintf(buffer, sizeof(buffer), "%s", currentColor);
                        ScreenBufferAppend(sbuf, buffer, len);
                    }
                }
                else if(highlight[j] == HIGHLIGHT_NORMAL)
                {
                    if(currentColor != NULL)
                    {
                        ScreenBufferAppend(sbuf, "\x1b[39m", 5);
                        currentColor = NULL;
                    }
                    ScreenBufferAppend(sbuf, &temp[j], 1);
                }
                else
                {
                    char* color = getSyntaxColor(highlight[j]);
                    if (color != currentColor)
                    {
                        currentColor = color;
                        char buffer[30];
                        int len = snprintf(buffer, sizeof(buffer), "%s", color);
                        ScreenBufferAppend(sbuf, buffer, len);
                    }
                    ScreenBufferAppend(sbuf, &temp[j], 1);
                }
            }

            ScreenBufferAppend(sbuf, "\x1b[39m", 5);
        }

        ScreenBufferAppend(sbuf, "\x1b[K", 3);
        ScreenBufferAppend(sbuf, "\r\n", 2);
    }
}

void    EditorDrawStatusBar(EditorConfiguration *config, ScreenBuffer* sbuf)
{
    ScreenBufferAppend(sbuf, "\x1b[7m", 4);

    char* filename = (config->filename == NULL) ? "[No File Opened]" : config->filename;

    char status[140], cursor[50];

    char* saveStatus = config->isSaved ? "" : "[UNSAVED]";
    int statusSize = snprintf(status, sizeof(status), "%s  %.50s ~ %ld lines", saveStatus, filename, config->textBuffer.numberofTextRows);

    int cursorSize = snprintf(cursor, sizeof(cursor), "%ld:%ld", config->cursorY + 1, config->cursorX + 1);

    if (statusSize > config->screenColumns)
        statusSize = config->screenColumns;

    ScreenBufferAppend(sbuf, status, statusSize);

    for (int i = statusSize; i < config->screenColumns; i++)
    {
        if (config->screenColumns - i == cursorSize)
        {
            ScreenBufferAppend(sbuf, cursor, cursorSize);
            break;
        }
        else
            ScreenBufferAppend(sbuf, " ", 1);
    }

    ScreenBufferAppend(sbuf, "\x1b[m", 3);
    ScreenBufferAppend(sbuf, "\r\n", 2);
}

void    EditorDrawMessageBar(EditorConfiguration *config, ScreenBuffer* sbuf)
{

    ScreenBufferAppend(sbuf, "\x1b[K", 3);
    int messageSize = strlen(config->statusMessage);

    if (messageSize > config->screenColumns)
        messageSize = config->screenColumns;

    if (messageSize && time(NULL) - config->statusMessageTime < STATUS_MESSAGE_DELAY)
        ScreenBufferAppend(sbuf, config->statusMessage, messageSize);
}

void    EditorRefreshScreen(EditorConfiguration *config)
{
    EditorScroll(config);

    ScreenBuffer sbuf = SCREEN_BUFFER_INIT;

    ScreenBufferAppend(&sbuf, "\x1b[?25l", 6);
    ScreenBufferAppend(&sbuf, "\x1b[H", 3);

    EditorDrawRows(config, &sbuf);
    EditorDrawStatusBar(config, &sbuf);
    EditorDrawMessageBar(config, &sbuf);

    char buffer[32];
    snprintf(buffer, sizeof(buffer), "\x1b[%ld;%ldH", (config->cursorY - config->rowOffset) + 1, (config->renderX - config->columnOffset) + 1);

    ScreenBufferAppend(&sbuf, buffer, strlen(buffer));

    ScreenBufferAppend(&sbuf, "\x1b[?25h", 6);

    write(STDOUT_FILENO, sbuf.string, sbuf.size);
    ScreenBufferFree(&sbuf);
}

/******* input ********/

char*   EditorPromptForInput(EditorConfiguration *config, char* prompt, void (*callBackFunction)(EditorConfiguration*, char*, int))
{
    int maxBufferSize = 150;
    char* buffer = malloc(maxBufferSize);
    buffer[0] = '\0';
    int bufferSize = 0;

    while (1)
    {
        EditorSetStatusMessage(config, prompt, buffer);
        EditorRefreshScreen(config);

        short int input = readKeypress();

        if (input == DELETE_KEY || input == CTRL_KEY('h') || input == BACKSPACE)
        {
            if (bufferSize > 0)
            {
                bufferSize--;
                buffer[bufferSize] = '\0';
            }
        }
        else if (input == '\x1b')
        {
            EditorSetStatusMessage(config, "");

            if (callBackFunction)
                callBackFunction(config, buffer, input);

            free(buffer);
            return NULL;
        }
        else if (input == '\r')
        {
            if(bufferSize > 0)
            {
                EditorSetStatusMessage(config, "");

                if (callBackFunction)
                    callBackFunction(config, buffer, input);

                return buffer;
            }
        }
        else if (!iscntrl(input) && input < 128)
        {
            if (bufferSize == maxBufferSize - 1)
            {
                maxBufferSize *= 2;
                buffer = realloc(buffer, maxBufferSize);
                if (buffer == NULL)
                    die("realloc");
            }
            buffer[bufferSize] = input;
            bufferSize++;
            buffer[bufferSize] = '\0';
        }

        if(callBackFunction)
            callBackFunction(config, buffer, input);
    }
}

void    EditorMoveCursor(EditorConfiguration *config, short int key)
{
    TextRow* row = (config->cursorY >= config->textBuffer.numberofTextRows)
                   ? NULL
                   : &config->textBuffer.textRow[config->cursorY];

    switch (key)
    {
        case ARROW_LEFT:
            if (config->cursorX != 0)
                config->cursorX--;
            else if (config->cursorY > 0)
            {
                config->cursorY--;
                config->cursorX = config->textBuffer.textRow[config->cursorY].textSize;
            }
            break;
        case ARROW_RIGHT:
            if (row != NULL && config->cursorX < row->textSize)
                config->cursorX++;
            else if (row != NULL && config->cursorX == row->textSize)
            {
                config->cursorY++;
                config->cursorX = 0;
            }
            break;
        case ARROW_UP:
            if (config->cursorY != 0)
                config->cursorY--;
            break;
        case ARROW_DOWN:
            if (config->cursorY < config->textBuffer.numberofTextRows)
                config->cursorY++;
            break;
    }

    row = (config->cursorY >= config->textBuffer.numberofTextRows)
          ? NULL
          : &config->textBuffer.textRow[config->cursorY];

    size_t rowSize = (row != NULL) ? row->textSize : 0;
    if (config->cursorX > rowSize)
        config->cursorX = rowSize;
}

void    EditorProcessKeypress(EditorConfiguration *config, Syntax HLDB[])
{
    static bool isQuiting = false;

    short int input = readKeypress();

    switch (input)
    {
        case '\r':
            EditorInsertNewLine(config);
            break;

        case CTRL_KEY('s'):
            EditorSaveToFile(config, HLDB);
            break;

        case CTRL_KEY('f'):
            EditorFind(config);
            break;

        case CTRL_KEY('q'):
            if (!config->isSaved && !isQuiting)
            {
                EditorSetStatusMessage(config, "File has unsaved changes! Press Ctrl-Q again to quit anyways.");
                isQuiting = true;
                return;
            }

            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            //EditorKill(config);
            exit(0);
            break;

        case HOME_KEY:
            config->cursorX = 0;
            break;

        case END_KEY:
            if (config->cursorY < config->textBuffer.numberofTextRows)
                config->cursorX = config->textBuffer.textRow[config->cursorY].textSize;
            break;

        case BACKSPACE:
        case CTRL_KEY('h'):
        case DELETE_KEY:
            if (input == DELETE_KEY)
                EditorMoveCursor(config, ARROW_RIGHT);
            EditorDeleteChar(config);
            break;

        case PAGE_UP:
        case PAGE_DOWN:
            if (input == PAGE_UP)
                config->cursorY = config->rowOffset;
            else if (input == PAGE_DOWN)
            {
                config->cursorY = config->rowOffset + config->screenRows - 1;
                if (config->cursorY > config->textBuffer.numberofTextRows)
                    config->cursorY = config->textBuffer.numberofTextRows;
            }

            for (int i = config->screenRows; i > 0; i--)
                EditorMoveCursor(config, input == PAGE_UP ? ARROW_UP : ARROW_DOWN);
            break;

        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
            EditorMoveCursor(config, input);
            break;

        case CTRL_KEY('l'):
        case '\x1b':
            break;

        default:
            EditorInsertChar(config, input);
            break;
    }

    isQuiting = false;
}


/******* Editor operations ********/

void    EditorInsertChar(EditorConfiguration *config, short int input)
{
    if (config->cursorY == config->textBuffer.numberofTextRows)
        TextBufferInsertTextRow(&config->textBuffer, config->textBuffer.numberofTextRows, "", 0);

    TextRowInsertChar(&config->textBuffer.textRow[config->cursorY], config->cursorX, input, config->textBuffer.syntax);
    config->isSaved = false;
    config->cursorX++;
}

void    EditorInsertNewLine(EditorConfiguration *config)
{
    if (config->cursorX == 0)
        TextBufferInsertTextRow(&config->textBuffer, config->cursorY, "", 0);
    else
    {
        TextRow* row = &config->textBuffer.textRow[config->cursorY];
        TextBufferInsertTextRow(&config->textBuffer, config->cursorY + 1, &row->text[config->cursorX], row->textSize - config->cursorX);
        row = &config->textBuffer.textRow[config->cursorY];
        row->textSize = config->cursorX;
        row->text[row->textSize] = '\0';
        TextRowUpdateRender(row);
        TextRowUpdateSyntax(row, config->textBuffer.syntax);
    }

    config->cursorY++;
    config->cursorX = 0;
    config->isSaved = false;
}

void    EditorDeleteChar(EditorConfiguration *config)
{
    if (config->cursorX == 0 && config->cursorY == 0)
        return;

    if (config->cursorY == config->textBuffer.numberofTextRows)
        return;

    TextRow* row = &config->textBuffer.textRow[config->cursorY];
    if (config->cursorX > 0)
    {
        TextRowDeleteChar(row, config->cursorX - 1, config->textBuffer.syntax);
        config->isSaved = false;
        config->cursorX--;
    }
    else
    {
        config->cursorX = config->textBuffer.textRow[config->cursorY - 1].textSize;
        TextRowAppendString(&config->textBuffer.textRow[config->cursorY - 1], row->text, row->textSize, config->textBuffer.syntax);
        TextBufferDeleteTextRow(&config->textBuffer, config->cursorY);
        config->isSaved = false;
        config->cursorY--;
    }
}

/******* text search ********/

void findCallBack(EditorConfiguration* config, char* query, int key)
{
    static ssize_t lastMatch = -1;
    static int direction = 1;
    static unsigned char* savedHighlight = NULL;
    static size_t savedRow;

    if(savedHighlight != NULL)
    {
        memcpy(config->textBuffer.textRow[savedRow].highlight, savedHighlight, config->textBuffer.textRow[savedRow].renderSize);
        free(savedHighlight);
        savedHighlight = NULL;
    }

    if (key == '\r' || key == '\x1b')
    {
        lastMatch = -1;
        direction = 1;
        return;
    }
    else if (key == ARROW_RIGHT || key == ARROW_DOWN)
        direction = 1;
    else if (key == ARROW_LEFT || key == ARROW_UP)
        direction = -1;
    else
    {
        lastMatch = -1;
        direction = 1;
    }

    if (lastMatch == -1)
        direction = 1;

    ssize_t currentMatch = lastMatch;
    for (size_t i = 0; i < config->textBuffer.numberofTextRows; i++)
    {
        currentMatch += direction;
        if (currentMatch == -1)
            currentMatch = config->textBuffer.numberofTextRows - 1;
        else if (currentMatch == config->textBuffer.numberofTextRows)
            currentMatch = 0;

        TextRow* row = &config->textBuffer.textRow[currentMatch];
        char* match = strstr(row->render, query);
        if (match != NULL)
        {
            lastMatch = currentMatch;
            config->cursorY = currentMatch;
            config->cursorX = TextRowGetCursorX(row, match - row->render);
            config->rowOffset = config->textBuffer.numberofTextRows;

            savedRow = currentMatch;
            savedHighlight = malloc(row->renderSize);
            memcpy(savedHighlight, row->highlight, row->renderSize);
            memset(&row->highlight[match - row->render], HIGHLIGHT_MATCH, strlen(query));

            break;
        }
    }
}

void EditorFind(EditorConfiguration* config)
{
    size_t cx = config->cursorX;
    size_t cy = config->cursorY;
    size_t columnOffset = config->columnOffset;
    size_t rowOffset = config->rowOffset;

    char* query = EditorPromptForInput(config, "Search for: %s", findCallBack);

    if (query != NULL)
        free(query);
    else
    {
        config->cursorX = cx;
        config->cursorY = cy;
        config->columnOffset = columnOffset;
        config->rowOffset = rowOffset;
    }
}

