/******* includes ********/

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <stdbool.h>
#include <signal.h>

/******* defines & enums ********/

#define NEO_VERSION "0.0.1"
#define TAB_STOP 4
#define STATUS_MESSAGE_DELAY 10
#define CTRL_KEY(k) ((k) & 0x1f)

enum Key
{
    BACKSPACE = 127,
    ARROW_LEFT = 2000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    DELETE_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN
};

enum Bool
{
    TRUE = 1,
    FALSE = 0
};

/******* global configuration data ********/

typedef struct TextRow
{
    size_t size;
    size_t renderSize;
    char* text;
    char* render;
} TextRow;

struct Configuration
{
    struct termios originalTermios;
    unsigned short int screenRows, screenColumns;
    size_t cursorX, cursorY;
    TextRow* textRow;
    size_t numberofTextRows;
    size_t renderX;
    size_t rowOffset;
    size_t columnOffset;
    char* filename;
    char statusMessage[200];
    time_t statusMessageTime;
    bool isSaved;
};

struct Configuration config;

/******* screen buffer structure to write to terminal from ********/

typedef struct SBuffer
{
    char* string;
    unsigned int size;
} SBuffer;

#define SBUFFER_INIT { NULL, 0 }

/******************************** All function prototypes ***************************************/

/******* initializing the terminal ********/

void die(const char* source);

void disableRawMode();

void enableRawMode();

short int readKeypress();

int getCursorPosition(unsigned short int* rows, unsigned short int* columns);

int getWindowSize(unsigned short int* rows, unsigned short int* columns);

/******* text row operations ********/

size_t getRenderX(TextRow* row, size_t cursorX);

size_t getCursorX(TextRow* row, size_t renderX);

void updateTextRow(TextRow* row);

void insertTextRow(size_t index, const char* str, size_t size);

void freeTextRow(TextRow* row);

void deleteTextRow(size_t index);

void insertCharIntoTextRow(TextRow* row, size_t index, short int input);

void appendStringToTextRow(TextRow* row, char* str, size_t size);

void deleteCharFromTextRow(TextRow* row, size_t index);

/******* editor operations ********/

void insertChar(short int input);

void insertNewLine();

void deleteChar();

/******* file i/o  ********/

char* TextRowToString(size_t* bufferSize);

void openFile(const char* filename);

void saveToFile();

/******* screen buffer ********/

void appendToSBuffer(SBuffer* sbuf, const char* string, size_t size);

void freeSBuffer(SBuffer* sbuf);

/******* text search ********/

void find();

void findCallBack(char* query, int key);

/******* output ********/

void scroll();

void clearStatusMessage();

void setStatusMessage(const char* fstring, ...);

void drawRows(SBuffer* sbuf);

void drawStatusBar(SBuffer* sbuf);

void drawMessageBar(SBuffer* sbuf);

void refreshScreen();

/******* input ********/

char* promptForInput(char* prompt, void (*callBackFunction)(char*, int));

void moveCursor(short int key);

void processKeypress();

/******* initializing the Editor ********/

void killEditor();

void initEditor();

void handleScreenResize(int signal);

/******* Main Function, where all the real work happens ********/

int main(int argc, char** argv)
{
    enableRawMode();
    initEditor();
    signal(SIGWINCH, handleScreenResize);

    if (argc >= 2)
        openFile(argv[1]);
    else
        openFile("neo.c"); // just for testing

    setStatusMessage("HELP: Ctrl-Q = quit");

    while (1)
    {
        refreshScreen();
        processKeypress();
    }

    return 0;
}

/******************************** All function inplementations ***********************************************/


/******* initializing the terminal ********/

void die(const char* source)
{
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    perror(source);
    exit(1);
}

void disableRawMode()
{
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &config.originalTermios) == -1)
        die("tcsetattr");

    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
}

void enableRawMode()
{
    if (tcgetattr(STDIN_FILENO, &config.originalTermios) == -1)
        die("tcgetattr");
    atexit(disableRawMode);

    struct termios raw = config.originalTermios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
        die("tcsetattr");
}

short int readKeypress()
{
    int readSize;
    char input;
    while ((readSize = read(STDIN_FILENO, &input, 1)) != 1)
    {
        if (readSize == -1 && errno != EAGAIN)
            die("read");
    }

    if (input == '\x1b')
    {
        char sequence[3];

        if (read(STDIN_FILENO, &sequence[0], 1) != 1)
            return '\x1b';
        if (read(STDIN_FILENO, &sequence[1], 1) != 1)
            return '\x1b';

        if (sequence[0] == '[')
        {
            if (sequence[1] >= '0' && sequence[1] <= '9')
            {
                if (read(STDIN_FILENO, &sequence[2], 1) != 1)
                    return '\x1b';
                if (sequence[2] == '~')
                {
                    switch (sequence[1])
                    {
                        case '1':
                            return HOME_KEY;
                        case '3':
                            return DELETE_KEY;
                        case '4':
                            return END_KEY;
                        case '5':
                            return PAGE_UP;
                        case '6':
                            return PAGE_DOWN;
                        case '7':
                            return HOME_KEY;
                        case '8':
                            return END_KEY;
                    }
                }
            }
            else
            {
                switch (sequence[1])
                {
                    case 'A':
                        return ARROW_UP;
                    case 'B':
                        return ARROW_DOWN;
                    case 'C':
                        return ARROW_RIGHT;
                    case 'D':
                        return ARROW_LEFT;
                    case 'H':
                        return HOME_KEY;
                    case 'F':
                        return END_KEY;
                }
            }
        }
        else if (sequence[0] == 'O')
        {
            switch (sequence[1])
            {
                case 'H':
                    return HOME_KEY;
                case 'F':
                    return END_KEY;
            }
        }

        return '\x1b';
    }
    else
        return input;
}

int getCursorPosition(unsigned short int* rows, unsigned short int* columns)
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
    if (sscanf(&buffer[2], "%hu;%hu", rows, columns) != 2)
        return -1;

    return 0;
}

int getWindowSize(unsigned short int* rows, unsigned short int* columns)
{
    struct winsize window;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &window) == -1 || window.ws_col == 0)
    {
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
            return -1;
        return getCursorPosition(rows, columns);
    }
    else
    {
        *columns = window.ws_col;
        *rows = window.ws_row;
        return 0;
    }
}

/******* text row operations ********/

size_t getRenderX(TextRow* row, size_t cursorX)
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

size_t getCursorX(TextRow* row, size_t renderX)
{
    size_t cx, rx = 0;

    for (cx = 0; cx < row->size; cx++)
    {
        if (row->text[cx] == '\t')
            rx += (TAB_STOP - 1) - (rx % TAB_STOP);
        rx++;

        if (rx > renderX)
            return cx;
    }
}

void updateTextRow(TextRow* row)
{
    unsigned int tabs = 0;
    for (size_t i = 0; i < row->size; i++)
    {
        if (row->text[i] == '\t')
            tabs++;
    }

    //
    free(row->render);
    char* temp = malloc(row->size + tabs * (TAB_STOP - 1) + 1);
    if (temp != NULL)
        row->render = temp;
    else
        die("malloc");

    size_t index = 0;
    for (size_t j = 0; j < row->size; j++)
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

void insertTextRow(size_t index, const char* str, size_t size)
{
    if (index > config.numberofTextRows)
        return;

    TextRow* temp = realloc(config.textRow, sizeof(TextRow) * (config.numberofTextRows + 1));
    if (temp != NULL)
        config.textRow = temp;
    else
        die("realloc");

    memmove(&config.textRow[index + 1], &config.textRow[index], sizeof(TextRow) * (config.numberofTextRows - index));

    config.textRow[index].size = size;
    config.textRow[index].text = malloc(size + 1);
    memcpy(config.textRow[index].text, str, size);
    config.textRow[index].text[size] = '\0';

    config.textRow[index].renderSize = 0;
    config.textRow[index].render = NULL;
    updateTextRow(&config.textRow[index]);

    config.numberofTextRows++;
    config.isSaved = FALSE;
}

void freeTextRow(TextRow* row)
{
    free(row->render);
    free(row->text);
}

void deleteTextRow(size_t index)
{
    if (index >= config.numberofTextRows)
        return;

    freeTextRow(&config.textRow[index]);

    memmove(&config.textRow[index], &config.textRow[index + 1], sizeof(TextRow) * (config.numberofTextRows - index - 1));

    config.numberofTextRows--;
    config.isSaved = FALSE;
}

void insertCharIntoTextRow(TextRow* row, size_t index, short int input)
{
    if (index > row->size)
        index = row->size;

    char* temp = realloc(row->text, row->size + 2);
    if (temp != NULL)
        row->text = temp;
    else
        die("realloc");

    memmove(&row->text[index + 1], &row->text[index], row->size - index + 1);

    row->size++;
    row->text[index] = input;
    updateTextRow(row);
    config.isSaved = FALSE;
}

void appendStringToTextRow(TextRow* row, char* str, size_t size)
{
    char* temp = realloc(row->text, row->size + size + 1);
    if (temp != NULL)
        row->text = temp;
    else
        die("realloc");

    memcpy(&row->text[row->size], str, size);

    row->size += size;
    row->text[row->size] = '\0';
    updateTextRow(row);
    config.isSaved = FALSE;
}

void deleteCharFromTextRow(TextRow* row, size_t index)
{
    if (index >= row->size)
        return;

    memmove(&row->text[index], &row->text[index + 1], row->size - index);

    row->size--;
    updateTextRow(row);
    config.isSaved = FALSE;
}

/******* editor operations ********/

void insertChar(short int input)
{
    if (config.cursorY == config.numberofTextRows)
        insertTextRow(config.numberofTextRows, "", 0);

    insertCharIntoTextRow(&config.textRow[config.cursorY], config.cursorX, input);
    config.cursorX++;
}

void insertNewLine()
{
    if (config.cursorX == 0)
        insertTextRow(config.cursorY, "", 0);
    else
    {
        TextRow* row = &config.textRow[config.cursorY];
        insertTextRow(config.cursorY + 1, &row->text[config.cursorX], row->size - config.cursorX);
        row = &config.textRow[config.cursorY];
        row->size = config.cursorX;
        row->text[row->size] = '\0';
        updateTextRow(row);
    }

    config.cursorY++;
    config.cursorX = 0;
    config.isSaved = FALSE;
}

void deleteChar()
{
    if (config.cursorX == 0 && config.cursorY == 0)
        return;

    if (config.cursorY == config.numberofTextRows)
        return;

    TextRow* row = &config.textRow[config.cursorY];
    if (config.cursorX > 0)
    {
        deleteCharFromTextRow(row, config.cursorX - 1);
        config.cursorX--;
    }
    else
    {
        config.cursorX = config.textRow[config.cursorY - 1].size;
        appendStringToTextRow(&config.textRow[config.cursorY - 1], row->text, row->size);
        deleteTextRow(config.cursorY);
        config.cursorY--;
    }
}

/******* file i/o  ********/

char* TextRowToString(size_t* bufferSize)
{
    size_t totalSize = 0;
    for (size_t i = 0; i < config.numberofTextRows; i++)
        totalSize += config.textRow[i].size + 1;
    *bufferSize = totalSize;

    char* buffer;
    if((buffer = malloc(totalSize)) == NULL)
        die("malloc");

    char* temp = buffer;

    for (size_t i = 0; i < config.numberofTextRows; i++)
    {
        memcpy(temp, config.textRow[i].text, config.textRow[i].size);

        temp += config.textRow[i].size;
        *temp = '\n';
        temp++;
    }

    return buffer;
}

void openFile(const char* filename)
{
    free(config.filename);

    char* temp = strdup(filename);
    if (temp != NULL)
        config.filename = temp;
    else
        die("strdup");


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

        insertTextRow(config.numberofTextRows, line, linelen);
    }

    free(line);
    fclose(file);
    config.isSaved = TRUE;
}

void saveToFile()
{
    if (config.filename == NULL)
    {
        config.filename = promptForInput("Save file as: %s", NULL);
        if (config.filename ==  NULL)
        {
            setStatusMessage("Save cancelled!");
            return;
        }
    }

    size_t bufferSize;
    char* buffer = TextRowToString(&bufferSize);

    int file = open(config.filename, O_RDWR | O_CREAT, 0644);

    if (file != -1)
    {
        if (ftruncate(file, bufferSize) != -1)
        {
            if (write(file, buffer, bufferSize) != -1)
            {
                close(file);
                free(buffer);
                config.isSaved = TRUE;
                setStatusMessage("%ld bytes written to disk.", bufferSize);
                return;
            }
        }
        close(file);
    }

    free(buffer);
    setStatusMessage("Save Failed! Error: %s", strerror(errno));
}

/******* screen buffer handling ********/

void appendToSBuffer(SBuffer* sbuf, const char* string, size_t size)
{
    char* temp = realloc(sbuf->string, sbuf->size + size);
    if (temp == NULL)
        die("realloc");

    memcpy(&temp[sbuf->size], string, size);

    sbuf->string = temp;
    sbuf->size += size;
}

void freeSBuffer(SBuffer* sbuf)
{
    free(sbuf->string);
    sbuf->size = 0;
}

/******* text search ********/

void findCallBack(char* query, int key)
{
    static ssize_t lastMatch = -1;
    static int direction = 1;

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
    for (size_t i = 0; i < config.numberofTextRows; i++)
    {
        currentMatch += direction;
        if (currentMatch == -1)
            currentMatch = config.numberofTextRows - 1;
        else if (currentMatch == config.numberofTextRows)
            currentMatch = 0;

        TextRow* row = &config.textRow[currentMatch];
        char* match = strstr(row->render, query);
        if (match != NULL)
        {
            lastMatch = currentMatch;
            config.cursorY = currentMatch;
            config.cursorX = getCursorX(row, match - row->render);
            config.rowOffset = config.numberofTextRows;
            break;
        }
    }
}

void find()
{
    size_t cx = config.cursorX;
    size_t cy = config.cursorY;
    size_t columnOffset = config.columnOffset;
    size_t rowOffset = config.rowOffset;

    char* query = promptForInput("Search for: %s", findCallBack);

    if (query != NULL)
        free(query);
    else
    {
        config.cursorX = cx;
        config.cursorY = cy;
        config.columnOffset = columnOffset;
        config.rowOffset = rowOffset;
    }
}

/******* output ********/

void scroll()
{
    config.renderX = 0;
    if (config.cursorY < config.numberofTextRows)
        config.renderX = getRenderX(&config.textRow[config.cursorY], config.cursorX);

    if (config.cursorY < config.rowOffset)
        config.rowOffset = config.cursorY;

    if (config.cursorY >= config.rowOffset + config.screenRows)
        config.rowOffset = config.cursorY - config.screenRows + 1;

    if (config.renderX < config.columnOffset)
        config.columnOffset = config.renderX;

    if (config.renderX >= config.columnOffset + config.screenColumns)
        config.columnOffset = config.renderX - config.screenColumns + 1;
}

void clearStatusMessage()
{
    memset(config.statusMessage, 0, sizeof(config.statusMessage));
}

void setStatusMessage(const char* fstring, ...)
{
    clearStatusMessage();
    va_list arg;
    va_start(arg, fstring);
    vsnprintf(config.statusMessage, sizeof(config.statusMessage), fstring, arg);
    va_end(arg);
    config.statusMessageTime = time(NULL);
}

void drawRows(SBuffer* sbuf)
{
    for (int i = 0; i < config.screenRows; i++)
    {
        size_t fileRow = i + config.rowOffset;
        if (fileRow >= config.numberofTextRows)
        {
            if (config.numberofTextRows == 0 && i == config.screenRows / 3)
            {
                char welcome[50];
                int welcomelen = snprintf(welcome, sizeof(welcome), "Neo Text Editor -- version %s", NEO_VERSION);

                if (welcomelen > config.screenColumns)
                    welcomelen = config.screenColumns;

                int padding = (config.screenColumns - welcomelen) / 2;
                if (padding > 0)
                {
                    appendToSBuffer(sbuf, ">", 1);
                    padding--;
                }
                while (padding)
                {
                    appendToSBuffer(sbuf, " ", 1);
                    padding--;
                }

                appendToSBuffer(sbuf, welcome, welcomelen);
            }
            else
                appendToSBuffer(sbuf, ">", 1);
        }
        else
        {
            ssize_t len = config.textRow[fileRow].renderSize - config.columnOffset;
            if (len < 0)
                len = 0;

            if (len > config.screenColumns)
                len = config.screenColumns;

            appendToSBuffer(sbuf, &config.textRow[fileRow].render[config.columnOffset], len);
        }

        appendToSBuffer(sbuf, "\x1b[K", 3);
        appendToSBuffer(sbuf, "\r\n", 2);
    }
}

void drawStatusBar(SBuffer* sbuf)
{
    appendToSBuffer(sbuf, "\x1b[7m", 4);

    char* filename = (config.filename == NULL) ? "[No File Opened]" : config.filename;

    char status[140], cursor[50];

    char* saveStatus = config.isSaved ? "" : "[UNSAVED]";
    int statusSize = snprintf(status, sizeof(status), "%s  %.50s ~ %ld lines", saveStatus, filename, config.numberofTextRows);

    int cursorSize = snprintf(cursor, sizeof(cursor), "%ld:%ld", config.cursorY + 1, config.cursorX + 1);

    if (statusSize > config.screenColumns)
        statusSize = config.screenColumns;

    appendToSBuffer(sbuf, status, statusSize);

    for (int i = statusSize; i < config.screenColumns; i++)
    {
        if (config.screenColumns - i == cursorSize)
        {
            appendToSBuffer(sbuf, cursor, cursorSize);
            break;
        }
        else
            appendToSBuffer(sbuf, " ", 1);
    }

    appendToSBuffer(sbuf, "\x1b[m", 3);
    appendToSBuffer(sbuf, "\r\n", 2);
}

void drawMessageBar(SBuffer* sbuf)
{

    appendToSBuffer(sbuf, "\x1b[K", 3);
    int messageSize = strlen(config.statusMessage);

    if (messageSize > config.screenColumns)
        messageSize = config.screenColumns;

    if (messageSize && time(NULL) - config.statusMessageTime < STATUS_MESSAGE_DELAY)
        appendToSBuffer(sbuf, config.statusMessage, messageSize);
}

void refreshScreen()
{
    scroll();

    struct SBuffer sbuf = SBUFFER_INIT;

    appendToSBuffer(&sbuf, "\x1b[?25l", 6);
    appendToSBuffer(&sbuf, "\x1b[H", 3);

    drawRows(&sbuf);
    drawStatusBar(&sbuf);
    drawMessageBar(&sbuf);

    char buffer[32];
    snprintf(buffer, sizeof(buffer), "\x1b[%ld;%ldH", (config.cursorY - config.rowOffset) + 1, (config.renderX - config.columnOffset) + 1);

    appendToSBuffer(&sbuf, buffer, strlen(buffer));

    appendToSBuffer(&sbuf, "\x1b[?25h", 6);

    write(STDOUT_FILENO, sbuf.string, sbuf.size);
    freeSBuffer(&sbuf);
}

/******* input ********/

char* promptForInput(char* prompt, void (*callBackFunction)(char*, int))
{
    int maxBufferSize = 150;
    char* buffer = malloc(maxBufferSize);
    buffer[0] = '\0';
    int bufferSize = 0;

    while (1)
    {
        setStatusMessage(prompt, buffer);
        refreshScreen();

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
            setStatusMessage("");

            if (callBackFunction)
                callBackFunction(buffer, input);

            free(buffer);
            return NULL;
        }
        else if (input == '\r')
        {
            if(bufferSize > 0)
            {
                setStatusMessage("");

                if (callBackFunction)
                    callBackFunction(buffer, input);

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
            callBackFunction(buffer, input);
    }
}

void moveCursor(short int key)
{
    TextRow* row = (config.cursorY >= config.numberofTextRows)
                   ? NULL
                   : &config.textRow[config.cursorY];

    switch (key)
    {
        case ARROW_LEFT:
            if (config.cursorX != 0)
                config.cursorX--;
            else if (config.cursorY > 0)
            {
                config.cursorY--;
                config.cursorX = config.textRow[config.cursorY].size;
            }
            break;
        case ARROW_RIGHT:
            if (row != NULL && config.cursorX < row->size)
                config.cursorX++;
            else if (row != NULL && config.cursorX == row->size)
            {
                config.cursorY++;
                config.cursorX = 0;
            }
            break;
        case ARROW_UP:
            if (config.cursorY != 0)
                config.cursorY--;
            break;
        case ARROW_DOWN:
            if (config.cursorY < config.numberofTextRows)
                config.cursorY++;
            break;
    }

    row = (config.cursorY >= config.numberofTextRows)
          ? NULL
          : &config.textRow[config.cursorY];

    size_t rowSize = (row != NULL) ? row->size : 0;
    if (config.cursorX > rowSize)
        config.cursorX = rowSize;
}

void processKeypress()
{
    static bool isQuiting = FALSE;

    short int input = readKeypress();

    switch (input)
    {
        case '\r':
            insertNewLine();
            break;

        case CTRL_KEY('s'):
            saveToFile();
            break;

        case CTRL_KEY('f'):
            find();
            break;

        case CTRL_KEY('q'):
            if (!config.isSaved && !isQuiting)
            {
                setStatusMessage("File has unsaved changes! Press Ctrl-Q again to quit anyways.");
                isQuiting = TRUE;
                return;
            }

            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;

        case HOME_KEY:
            config.cursorX = 0;
            break;

        case END_KEY:
            if (config.cursorY < config.numberofTextRows)
                config.cursorX = config.textRow[config.cursorY].size;
            break;

        case BACKSPACE:
        case CTRL_KEY('h'):
        case DELETE_KEY:
            if (input == DELETE_KEY)
                moveCursor(ARROW_RIGHT);
            deleteChar();
            break;

        case PAGE_UP:
        case PAGE_DOWN:
            if (input == PAGE_UP)
                config.cursorY = config.rowOffset;
            else if (input == PAGE_DOWN)
            {
                config.cursorY = config.rowOffset + config.screenRows - 1;
                if (config.cursorY > config.numberofTextRows)
                    config.cursorY = config.numberofTextRows;
            }

            for (int i = config.screenRows; i > 0; i--)
                moveCursor(input == PAGE_UP ? ARROW_UP : ARROW_DOWN);
            break;

        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
            moveCursor(input);
            break;

        case CTRL_KEY('l'):
        case '\x1b':
            break;

        default:
            insertChar(input);
            break;
    }

    isQuiting = FALSE;
}

/******* initializing the editor ********/

void killEditor()
{
    free(config.filename);
    for (size_t i = 0; i < config.numberofTextRows; i++)
    {
        free(config.textRow[i].text);
        free(config.textRow[i].render);
    }
    free(config.textRow);
}

void initEditor()
{
    config.cursorX = 0;
    config.cursorY = 0;
    config.renderX = 0;
    config.numberofTextRows = 0;
    config.rowOffset = 0;
    config.columnOffset = 0;
    config.textRow = NULL;
    config.filename = NULL;
    config.statusMessage[0] = '\0';
    config.statusMessageTime = 0;
    config.isSaved = TRUE;

    if (getWindowSize(&config.screenRows, &config.screenColumns) == -1)
        die("getWindowSize");

    config.screenRows -= 2;

    atexit(killEditor);
}

void handleScreenResize(int signal)
{
    if (getWindowSize(&config.screenRows, &config.screenColumns) == -1)
        die("getWindowSize");

    config.screenRows -= 2;
    refreshScreen();
}
