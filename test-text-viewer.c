/*** includes ***/

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <time.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

/*** defines ***/

#define NEO_VERSION "0.0.1"
#define TAB_STOP 4
#define CTRL_KEY(k) ((k) & 0x1f)

enum Key
{
    ARROW_LEFT = 2000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    DEL_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN
};

/*** global configuration data ***/

typedef struct TextRow
{
    size_t length;
    size_t renderLength;
    char* text;
    char* render;
} TextRow;

struct Configuration
{
    size_t cursorX, cursorY;
    unsigned short int screenRows, screenColumns;
    size_t numberofRows;
    TextRow* textRow;
    size_t renderX;
    size_t rowOffset;
    size_t columnOffset;
    struct termios originalTermios;
};

struct Configuration config;

/*** terminal ***/

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
    int readLength;
    char input;
    while ((readLength = read(STDIN_FILENO, &input, 1)) != 1)
    {
        if (readLength == -1 && errno != EAGAIN)
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
                            return DEL_KEY;
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
    if (sscanf(&buffer[2], "%hd;%hd", rows, columns) != 2)
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

/*** text row operations ***/

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

void updateTextRow(TextRow* row)
{
    unsigned int tabs = 0;
    for (size_t i = 0; i < row->length; i++)
    {
        if (row->text[i] == '\t')
            tabs++;
    }

    free(row->render);
    row->render = malloc(row->length + tabs * (TAB_STOP - 1) + 1);

    size_t index = 0;
    for (size_t j = 0; j < row->length; j++)
    {
        if (row->text[j] == '\t')
        {
            row->render[index++] = ' ';
            while (index % TAB_STOP != 0)
                row->render[index++] = ' ';
        }
        else
            row->render[index++] = row->text[j];

    }

    row->render[index] = '\0';
    row->renderLength = index;
}

void appendTextRow(const char* str, size_t length)
{
    config.textRow = realloc(config.textRow, sizeof(TextRow) * (config.numberofRows + 1));

    size_t at = config.numberofRows;
    config.textRow[at].length = length;
    config.textRow[at].text = malloc(length + 1);
    memcpy(config.textRow[at].text, str, length);
    config.textRow[at].text[length] = '\0';

    config.textRow[at].renderLength = 0;
    config.textRow[at].render = NULL;
    updateTextRow(&config.textRow[at]);

    config.numberofRows++;
}

/*** file i/o  ***/

void openFile(const char* filename)
{
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
        appendTextRow(line, linelen);
    }

    free(line);
    fclose(file);
}

/*** string buffer to write from ***/

struct SBuffer
{
    char* string;
    unsigned int length;
};

#define SBUFFER_INIT {NULL, 0}

void appendToSBuffer(struct SBuffer* sbuf, const char* string, unsigned int length)
{
    char* temp = realloc(sbuf->string, sbuf->length + length);

    if (temp == NULL)
        return;
    memcpy(&temp[sbuf->length], string, length);
    sbuf->string = temp;
    sbuf->length += length;
}

void freeSBuffer(struct SBuffer* sbuf)
{
    free(sbuf->string);
    sbuf->length = 0;
}

/*** output ***/

void scroll()
{
    config.renderX = 0;
    if (config.cursorY < config.numberofRows)
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

void drawRows(struct SBuffer* sbuf)
{
    for (int i = 0; i < config.screenRows; i++)
    {
        size_t fileRow = i + config.rowOffset;
        if (fileRow >= config.numberofRows)
        {
            if (config.numberofRows == 0 && i == config.screenRows / 3)
            {
                char welcome[50];
                int welcomelen = snprintf(welcome, sizeof(welcome), "Neo Text Editor -- version %s", NEO_VERSION);
                if (welcomelen > config.screenColumns)
                    welcomelen = config.screenColumns;
                int padding = (config.screenColumns - welcomelen) / 2;
                if (padding)
                {
                    appendToSBuffer(sbuf, ">", 1);
                    padding--;
                }
                while (padding--)
                    appendToSBuffer(sbuf, " ", 1);
                appendToSBuffer(sbuf, welcome, welcomelen);
            }
            else
                appendToSBuffer(sbuf, ">", 1);
        }
        else
        {
            ssize_t len = config.textRow[fileRow].renderLength - config.columnOffset;
            if (len < 0)
                len = 0;
            if (len > config.screenColumns)
                len = config.screenColumns;
            appendToSBuffer(sbuf, &config.textRow[fileRow].render[config.columnOffset], len);
        }

        appendToSBuffer(sbuf, "\x1b[K", 3);
        if (i < config.screenRows - 1)
            appendToSBuffer(sbuf, "\r\n", 2);
    }
}

void refreshScreen()
{
    scroll();

    struct SBuffer sbuf = SBUFFER_INIT;

    appendToSBuffer(&sbuf, "\x1b[?25l", 6);
    appendToSBuffer(&sbuf, "\x1b[H", 3);

    drawRows(&sbuf);

    char buffer[32];
    snprintf(buffer, sizeof(buffer), "\x1b[%ld;%ldH", (config.cursorY - config.rowOffset) + 1, (config.renderX - config.columnOffset) + 1);
    appendToSBuffer(&sbuf, buffer, strlen(buffer));

    appendToSBuffer(&sbuf, "\x1b[?25h", 6);

    write(STDOUT_FILENO, sbuf.string, sbuf.length);
    freeSBuffer(&sbuf);
}

/*** input ***/

void moveCursor(short int key)
{
    TextRow* row = (config.cursorY >= config.numberofRows) ? NULL : &config.textRow[config.cursorY];

    switch (key)
    {
        case ARROW_LEFT:
            if (config.cursorX != 0)
                config.cursorX--;
            else if (config.cursorY > 0)
            {
                config.cursorY--;
                config.cursorX = config.textRow[config.cursorY].length;
            }
            break;
        case ARROW_RIGHT:
            if (row != NULL && config.cursorX < row->length)
                config.cursorX++;
            else if (row != NULL && config.cursorX == row->length)
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
            if (config.cursorY < config.numberofRows)
                config.cursorY++;
            break;
    }

    row = (config.cursorY >= config.numberofRows) ? NULL : &config.textRow[config.cursorY];
    size_t rowLength = (row != NULL) ? row->length : 0;
    if (config.cursorX > rowLength)
        config.cursorX = rowLength;

}

void processKeypress()
{
    short int input = readKeypress();

    switch (input)
    {
        case CTRL_KEY('q'):
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;

        case HOME_KEY:
            config.cursorX = 0;
            break;

        case END_KEY:
            if (config.cursorY < config.numberofRows)
                config.cursorX = config.textRow[config.cursorY].length;
            break;

        case PAGE_UP:
        case PAGE_DOWN:
            if (input == PAGE_UP)
                config.cursorY = config.rowOffset;
            else if (input == PAGE_DOWN)
            {
                config.cursorY = config.rowOffset + config.screenRows - 1;
                if (config.cursorY > config.numberofRows)
                    config.cursorY = config.numberofRows;
            }

            for(int i = config.screenRows; i > 0; i--)
                moveCursor(input == PAGE_UP ? ARROW_UP : ARROW_DOWN);
            break;

        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
            moveCursor(input);
            break;
    }
}

/*** init ***/

void initEditor()
{
    config.cursorX = 0;
    config.cursorY = 0;
    config.renderX = 0;
    config.numberofRows = 0;
    config.rowOffset = 0;
    config.columnOffset = 0;
    config.textRow = NULL;

    if (getWindowSize(&config.screenRows, &config.screenColumns) == -1)
        die("getWindowSize");
}

int main(int argc, char** argv)
{
    enableRawMode();
    initEditor();

    if (argc >= 2)
        openFile(argv[1]);

    while (1)
    {
        refreshScreen();
        processKeypress();
    }

    return 0;
}                                                                   
