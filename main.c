#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <ncurses.h>

#include <math.h>

int countDigits(int num)
{
    if (num < 0)
    {
        num = -num;
    }

    if (num == 0)
    {
        return 1;
    }
    return (int)log10(num) + 1;
}

 
#define CTRL_KEY(k) ((k) & 0x1f)
#define BACKSPACE 127
#define ENTER '\r'
#define ARROW_LEFT 1000
#define ARROW_RIGHT 1001
#define ARROW_UP 1002
#define ARROW_DOWN 1003

 
struct textEditorConfig
{
    int cursorX, cursorY;           // Cursor X and Y position
    int rowOffset;                  // Row offset for scrolling
    int colOffset;                  // Column offset for scrolling
    int terminalRows;               // Number of rows in the terminal
    int terminalCols;               // Number of columns in the terminal
    int totalRows;                  // Number of rows in the buffer
    char **textRows;                // Buffer holding lines of text
    char *saveFilename;             // The filename to save to
    struct termios originalTermios; // Original terminal attributes
};

struct textEditorConfig editorConfig;

/* Disable raw mode */
void disableRawInput()
{
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &editorConfig.originalTermios);
    printf("\x1b[?25h"); // Show cursor
}

void handleSigint(int sig)
{
    endwin(); // End ncurses mode
    exit(0);
}

/* Enable raw mode */
void enableRawInput()
{
    tcgetattr(STDIN_FILENO, &editorConfig.originalTermios);
    atexit(disableRawInput);

    struct termios raw = editorConfig.originalTermios;
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
    raw.c_iflag &= ~(BRKINT | INPCK | ISTRIP | IXON | ICRNL);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

/* Get window size */
int getTerminalSize(int *rows, int *cols)
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0)
    {
        return -1;
    }
    else
    {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

/* Read key press with support for arrow keys */
int readKeyInput()
{
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1)
        ;

    if (c == '\x1b')
    { // Escape sequence
        char seq[3];
        if (read(STDIN_FILENO, &seq[0], 1) != 1)
            return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1)
            return '\x1b';

        if (seq[0] == '[')
        {
            switch (seq[1])
            {
            case 'A':
                return ARROW_UP;
            case 'B':
                return ARROW_DOWN;
            case 'C':
                return ARROW_RIGHT;
            case 'D':
                return ARROW_LEFT;
            }
        }
        return '\x1b';
    }
    else
    {
        return c;
    }
}

void moveCursor(int key)
{
    switch (key)
    {
    case ARROW_LEFT:
        if (editorConfig.cursorX > 0)
        {
            editorConfig.cursorX--;
        }
        else if (editorConfig.cursorY > 0)
        { // Move to end of previous line if possible
            editorConfig.cursorY--;
            editorConfig.cursorX = strlen(editorConfig.textRows[editorConfig.cursorY]);
        }
        break;
    case ARROW_RIGHT:
        if (editorConfig.cursorX < strlen(editorConfig.textRows[editorConfig.cursorY]))
        {
            editorConfig.cursorX++;
        }
        else if (editorConfig.cursorY < editorConfig.totalRows - 1)
        { // Move to start of next line
            editorConfig.cursorY++;
            editorConfig.cursorX = 0;
        }
        break;
    case ARROW_UP:
        if (editorConfig.cursorY > 0)
        {
            editorConfig.cursorY--;
            if (editorConfig.cursorX > strlen(editorConfig.textRows[editorConfig.cursorY]))
                editorConfig.cursorX = strlen(editorConfig.textRows[editorConfig.cursorY]);
        }
        break;
    case ARROW_DOWN:
        if (editorConfig.cursorY < editorConfig.totalRows - 1)
        {
            editorConfig.cursorY++;
            if (editorConfig.cursorX > strlen(editorConfig.textRows[editorConfig.cursorY]))
                editorConfig.cursorX = strlen(editorConfig.textRows[editorConfig.cursorY]);
        }
        break;
    }

    if (editorConfig.cursorY < editorConfig.rowOffset)
    {
        editorConfig.rowOffset = editorConfig.cursorY;
    }
    else if (editorConfig.cursorY >= editorConfig.rowOffset + editorConfig.terminalRows)
    {
        editorConfig.rowOffset = editorConfig.cursorY - editorConfig.terminalRows + 1;
    }
    if (editorConfig.cursorX < editorConfig.colOffset)
    {
        editorConfig.colOffset = editorConfig.cursorX;
    }
    else if (editorConfig.cursorX >= editorConfig.colOffset + editorConfig.terminalCols)
    {
        editorConfig.colOffset = editorConfig.cursorX - editorConfig.terminalCols + 1;
    }
}

void refreshScreen()
{
    clear();

    for (int y = 0; y < editorConfig.terminalRows; y++)
    {
        int fileRow = y + editorConfig.rowOffset;
        if (fileRow >= editorConfig.totalRows)
        {
            mvprintw(y, 0, "~"); 
        }
        else
        {

            attron(COLOR_PAIR(1)); // Activate color pair 1
            mvprintw(y, 0, "%d ", fileRow + 1);
            attroff(COLOR_PAIR(1));
            // attron(COLOR_PAIR(1)); // Activate color pair 1
            mvprintw(y, 2, "%s", editorConfig.textRows[fileRow]);
        }
    }

    int charCount = 0;
    for (int i = 0; i < editorConfig.totalRows; i++)
    {
        charCount += strlen(editorConfig.textRows[i]);
    }
    int digit_offset = countDigits(editorConfig.totalRows + 1) + 1;

    mvprintw(LINES - 2, 0, "Characters: %d | Rows: %d | Position: %d:%d", charCount, editorConfig.totalRows, editorConfig.cursorY + 1, editorConfig.cursorX + 1);
    move(editorConfig.cursorY - editorConfig.rowOffset, editorConfig.cursorX + digit_offset); // Move cursor to its position
    refresh();
}

void insertRow(int at, char *s, size_t len)
{
    if (at < 0 || at > editorConfig.totalRows)
        return;

    editorConfig.textRows = realloc(editorConfig.textRows, sizeof(char *) * (editorConfig.totalRows + 1));
    memmove(&editorConfig.textRows[at + 1], &editorConfig.textRows[at], sizeof(char *) * (editorConfig.totalRows - at));

    editorConfig.textRows[at] = malloc(len + 1);
    memcpy(editorConfig.textRows[at], s, len);
    editorConfig.textRows[at][len] = '\0';
    editorConfig.totalRows++;
}

void insertChar(int c)
{
    if (editorConfig.cursorY >= editorConfig.totalRows)
    {
        insertRow(editorConfig.totalRows, "", 0);
    }

    char *row = editorConfig.textRows[editorConfig.cursorY];
    size_t linelen = strlen(row);
    row = realloc(row, linelen + 2); // +2 for new char and null-terminator
    memmove(&row[editorConfig.cursorX + 1], &row[editorConfig.cursorX], linelen - editorConfig.cursorX + 1);
    row[editorConfig.cursorX] = c;
    editorConfig.textRows[editorConfig.cursorY] = row;
    editorConfig.cursorX++;

    // Wrap to the next line if we reach the end of the current line
    if (editorConfig.cursorX >= editorConfig.terminalCols)
    {
        editorConfig.cursorX = 0;
        if (editorConfig.cursorY < editorConfig.totalRows - 1)
        {
            editorConfig.cursorY++;
        }
        else
        {
            insertRow(editorConfig.totalRows, "", 0);
            editorConfig.cursorY++;
        }
    }
}

/* Insert a new line at the cursor position */
void insertNewLine()
{
    if (editorConfig.cursorX == 0)
    {
        insertRow(editorConfig.cursorY, "", 0);
    }
    else
    {
        char *row = editorConfig.textRows[editorConfig.cursorY];
        insertRow(editorConfig.cursorY + 1, &row[editorConfig.cursorX], strlen(row) - editorConfig.cursorX);
        editorConfig.textRows[editorConfig.cursorY][editorConfig.cursorX] = '\0';
    }
    editorConfig.cursorY++;
    editorConfig.cursorX = 0;
}

/* Delete a character at the cursor position */
void deleteChar()
{
    if (editorConfig.cursorY >= editorConfig.totalRows || (editorConfig.cursorX == 0 && editorConfig.cursorY == 0))
        return;

    char *row = editorConfig.textRows[editorConfig.cursorY];
    if (editorConfig.cursorX > 0)
    {
        memmove(&row[editorConfig.cursorX - 1], &row[editorConfig.cursorX], strlen(row) - editorConfig.cursorX + 1);
        editorConfig.cursorX--;
    }
    else
    {
        // Merge with previous row
        size_t prev_len = strlen(editorConfig.textRows[editorConfig.cursorY - 1]);
        size_t curr_len = strlen(row);
        editorConfig.textRows[editorConfig.cursorY - 1] = realloc(editorConfig.textRows[editorConfig.cursorY - 1], prev_len + curr_len + 1);
        memcpy(&editorConfig.textRows[editorConfig.cursorY - 1][prev_len], row, curr_len + 1);
        free(editorConfig.textRows[editorConfig.cursorY]);
        memmove(&editorConfig.textRows[editorConfig.cursorY], &editorConfig.textRows[editorConfig.cursorY + 1], sizeof(char *) * (editorConfig.totalRows - editorConfig.cursorY - 1));
        editorConfig.totalRows--;
        editorConfig.cursorY--;
        editorConfig.cursorX = prev_len; // Set cursorX to the end of the previous line
    }
}

/* Save the editor content to a file */
void saveFile()
{
    if (!editorConfig.saveFilename)
    {
 
        char filename[256];
        mvprintw(LINES - 1, 0, "Save as: ");
        echo();
        getnstr(filename, sizeof(filename) - 1);
        noecho();
        editorConfig.saveFilename = strdup(filename);
    }

    FILE *fp = fopen(editorConfig.saveFilename, "w");
    if (!fp)
    {
        perror("Unable to save file");
        return;
    }

    for (int i = 0; i < editorConfig.totalRows; i++)
    {
        fprintf(fp, "%s\n", editorConfig.textRows[i]);
    }

    fclose(fp);
    mvprintw(LINES - 1, 0, "File saved as %s              ", editorConfig.saveFilename);  
}

 
void processKeypress()
{
    int c = readKeyInput();

    switch (c)
    {
    case CTRL_KEY('q'): // Quit
        disableRawInput();
        printf("\x1b[2J\x1b[H"); // Clear the screen
        exit(0);
        break;
    case CTRL_KEY('s'): // Save
        saveFile();
        break;
    case CTRL('c'): // CTRL+C to quit
        endwin();   // End ncurses mode
        exit(0);
    case BACKSPACE:
        deleteChar();
        break;
    case ENTER:
        insertNewLine();
        break;
    case ARROW_LEFT:
    case ARROW_RIGHT:
    case ARROW_UP:
    case ARROW_DOWN:
        moveCursor(c);
        break;
    default:
        if (!iscntrl(c))
        {
            insertChar(c);
        }
        break;
    }
}

 
void initializeEditor()
{
    editorConfig.cursorX = 0;
    editorConfig.cursorY = 0;
    editorConfig.rowOffset = 0;
    editorConfig.colOffset = 0;
    editorConfig.totalRows = 0;
    editorConfig.textRows = NULL;
    editorConfig.saveFilename = NULL;

    if (getTerminalSize(&editorConfig.terminalRows, &editorConfig.terminalCols) == -1)
    {
        perror("getTerminalSize");
        exit(1);
    }
}

 
int main()
{
    signal(SIGINT, handleSigint); // Handle CTRL+C
    initscr();                     
    noecho();                     
    cbreak();                      
    keypad(stdscr, TRUE);          

    // start_color();  

    init_color(COLOR_CYAN, 0, 255, 255); 
    init_pair(1, COLOR_CYAN, COLOR_BLACK);    

    initializeEditor();

    enableRawInput();
    initializeEditor();

    while (1)
    {
        refreshScreen();
        processKeypress();
    }

    return 0;
}
