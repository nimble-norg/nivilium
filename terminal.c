#include "vi.h"

static void die(const char *msg)
{
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H",  3);
    perror(msg);
    exit(1);
}

void disable_raw_mode(void)
{
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios);
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H",  3);
}

void reenable_raw_mode(void)
{
    struct termios raw = E.orig_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |=   CS8;
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN]  = 0;
    raw.c_cc[VTIME] = 1;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

void enable_raw(void)
{
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr");
    atexit(disable_raw_mode);
    reenable_raw_mode();
}

void get_window_size(void)
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        E.rows = 22;
        E.cols = 80;
    } else {
        E.rows = ws.ws_row - 2;
        E.cols = ws.ws_col;
    }
    if (E.rows < 1)  E.rows = 1;
    if (E.cols < 10) E.cols = 10;
}

int read_key(void)
{
    ssize_t nread;
    char c;

    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno == EINTR) {
            check_signals();
            continue;
        }
        if (nread < 0) die("read");
        check_signals();
    }

    if (c != '\x1b') return (unsigned char)c;

    char seq[6] = {0};
    if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
    if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

    if (seq[0] == '[') {
        if (seq[1] >= '0' && seq[1] <= '9') {
            if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
            if (seq[2] == '~') {
                switch (seq[1]) {
                    case '1': return KEY_HOME;
                    case '4': return KEY_END;
                    case '5': return KEY_PPAGE;
                    case '6': return KEY_NPAGE;
                    case '3': return KEY_DEL;
                    case '7': return KEY_HOME;
                    case '8': return KEY_END;
                }
            }
            if (seq[2] == ';') {
                char seq2[3] = {0};
                if (read(STDIN_FILENO, &seq2[0], 1) != 1) return '\x1b';
                if (read(STDIN_FILENO, &seq2[1], 1) != 1) return '\x1b';
            }
            return '\x1b';
        }
        switch (seq[1]) {
            case 'A': return KEY_UP;
            case 'B': return KEY_DOWN;
            case 'C': return KEY_RIGHT;
            case 'D': return KEY_LEFT;
            case 'H': return KEY_HOME;
            case 'F': return KEY_END;
        }
    }

    if (seq[0] == 'O') {
        switch (seq[1]) {
            case 'H': return KEY_HOME;
            case 'F': return KEY_END;
            case 'A': return KEY_UP;
            case 'B': return KEY_DOWN;
            case 'C': return KEY_RIGHT;
            case 'D': return KEY_LEFT;
        }
    }

    return '\x1b';
}
