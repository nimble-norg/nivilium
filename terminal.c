#include "vi.h"

static void die(const char *msg)
{
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H",  3);
    perror(msg);
    exit(1);
}

static void disable_raw(void)
{
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios);
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H",  3);
}

void enable_raw(void)
{
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr");
    atexit(disable_raw);

    struct termios raw = E.orig_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |=   CS8;
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN]  = 0;
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

static void sigwinch_handler(int sig)
{
    (void)sig;
    E.need_resize = 1;
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

    signal(SIGWINCH, sigwinch_handler);
}

int read_key(void)
{
    ssize_t nread;
    char c;

    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread < 0) die("read");
        if (E.need_resize) {
            E.need_resize = 0;
            get_window_size();
            draw_screen();
        }
    }

    if (c == '\x1b') {
        char seq[4] = {0, 0, 0, 0};
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

        if (seq[0] == '[') {
            switch (seq[1]) {
                case 'A': return KEY_UP;
                case 'B': return KEY_DOWN;
                case 'C': return KEY_RIGHT;
                case 'D': return KEY_LEFT;
            }
        }
        return '\x1b';
    }

    return (unsigned char)c;
}
