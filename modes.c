#include "vi.h"
#include <ctype.h>

void clamp_cursor(void)
{
    if (E.cy < 0)         E.cy = 0;
    if (E.cy >= E.nlines) E.cy = E.nlines - 1;

    int llen = E.lines[E.cy].len;

    if (E.mode == MODE_INSERT) {
        if (E.cx > llen) E.cx = llen;
    } else {
        if (E.cx >= llen) E.cx = llen > 0 ? llen - 1 : 0;
    }
    if (E.cx < 0) E.cx = 0;
}

static int is_word(char c)  { return isalnum((unsigned char)c) || c == '_'; }
static int is_blank(char c) { return c == ' ' || c == '\t'; }

static void motion_w(int big)
{
    int row = E.cy, col = E.cx;
    const char *d = E.lines[row].data;
    int         l = E.lines[row].len;

    if (col < l) {
        if (big) {
            while (col < l && !is_blank(d[col])) col++;
        } else {
            if (is_word(d[col]))
                while (col < l &&  is_word(d[col])) col++;
            else
                while (col < l && !is_word(d[col]) && !is_blank(d[col])) col++;
        }
        while (col < l && is_blank(d[col])) col++;
        if (col < l) { E.cx = col; return; }
    }

    if (row + 1 < E.nlines) {
        row++;
        d   = E.lines[row].data;
        l   = E.lines[row].len;
        col = 0;
        while (col < l && is_blank(d[col])) col++;
        E.cy = row;
        E.cx = col < l ? col : 0;
    }
}

static void motion_b(int big)
{
    int row = E.cy, col = E.cx;

    if (col == 0) {
        if (row == 0) return;
        row--;
        col = E.lines[row].len > 0 ? E.lines[row].len - 1 : 0;
        E.cy = row; E.cx = col;
        return;
    }
    col--;

    const char *d = E.lines[row].data;
    while (col > 0 && is_blank(d[col])) col--;

    if (big) {
        while (col > 0 && !is_blank(d[col - 1])) col--;
    } else {
        if (is_word(d[col]))
            while (col > 0 &&  is_word(d[col - 1])) col--;
        else
            while (col > 0 && !is_word(d[col - 1]) && !is_blank(d[col - 1])) col--;
    }
    E.cy = row; E.cx = col;
}

static void motion_e(int big)
{
    int row = E.cy, col = E.cx;
    const char *d = E.lines[row].data;
    int         l = E.lines[row].len;

    if (l == 0) return;

    if (col + 1 < l) {
        col++;
    } else {
        if (row + 1 < E.nlines) {
            row++;
            d   = E.lines[row].data;
            l   = E.lines[row].len;
            col = 0;
        } else {
            return;
        }
    }

    while (col < l && is_blank(d[col])) col++;

    if (big) {
        while (col + 1 < l && !is_blank(d[col + 1])) col++;
    } else {
        if (col < l) {
            if (is_word(d[col]))
                while (col + 1 < l &&  is_word(d[col + 1])) col++;
            else
                while (col + 1 < l && !is_word(d[col + 1]) && !is_blank(d[col + 1])) col++;
        }
    }

    E.cy = row;
    E.cx = col < l ? col : (l > 0 ? l - 1 : 0);
}

static void do_yank_line(void)
{
    Line *l = &E.lines[E.cy];
    free(E.yank_data);
    E.yank_data = malloc(l->len + 1);
    memcpy(E.yank_data, l->data, l->len);
    E.yank_len  = l->len;
    snprintf(E.statusmsg, sizeof(E.statusmsg), "1 line yanked");
}

static void do_paste(void)
{
    if (!E.yank_data) return;
    save_undo();
    insert_line_at(E.cy + 1);
    E.cy++;
    Line *l = &E.lines[E.cy];
    if (E.yank_len + 1 > l->cap) {
        l->cap  = E.yank_len + LINE_INIT;
        l->data = realloc(l->data, l->cap);
    }
    memcpy(l->data, E.yank_data, E.yank_len);
    l->len  = E.yank_len;
    E.cx    = 0;
    E.dirty = 1;
}

static void do_search(void)
{
    if (E.search_len <= 0) return;

    int slen = E.search_len;

    for (int i = E.cy; i < E.nlines; i++) {
        Line *l     = &E.lines[i];
        int   start = (i == E.cy) ? E.cx + 1 : 0;
        for (int j = start; j <= l->len - slen; j++) {
            if (memcmp(l->data + j, E.search_buf, slen) == 0) {
                E.cy = i; E.cx = j;
                snprintf(E.statusmsg, sizeof(E.statusmsg),
                         "/%.*s", slen, E.search_buf);
                return;
            }
        }
    }

    for (int i = 0; i <= E.cy; i++) {
        Line *l     = &E.lines[i];
        int   end_j = (i == E.cy) ? E.cx : l->len;
        for (int j = 0; j <= end_j - slen; j++) {
            if (memcmp(l->data + j, E.search_buf, slen) == 0) {
                E.cy = i; E.cx = j;
                snprintf(E.statusmsg, sizeof(E.statusmsg),
                         "search wrapped: /%.*s", slen, E.search_buf);
                return;
            }
        }
    }

    snprintf(E.statusmsg, sizeof(E.statusmsg),
             "E486: Pattern not found: %.*s", slen, E.search_buf);
}

static void process_ex_command(void)
{
    E.ex_buf[E.ex_len] = '\0';

    char *cmd = E.ex_buf;
    while (*cmd == ' ') cmd++;

    if (cmd[0] == 'q' && cmd[1] == '!') { exit(0); }

    if (strcmp(cmd, "q") == 0) {
        if (E.dirty)
            snprintf(E.statusmsg, sizeof(E.statusmsg),
                     "E37: No write since last change (use :q! to override)");
        else
            exit(0);
        return;
    }

    if (strncmp(cmd, "w", 1) == 0 &&
        (cmd[1] == '\0' || cmd[1] == ' ' || cmd[1] == '!')) {
        char *arg = cmd + 1;
        if (*arg == '!') arg++;
        while (*arg == ' ') arg++;
        if (*arg != '\0') {
            strncpy(E.filename, arg, sizeof(E.filename) - 1);
            E.filename[sizeof(E.filename) - 1] = '\0';
        }
        int do_quit = (strchr(cmd, 'q') && strchr(cmd, 'q') > cmd);
        if (E.filename[0]) { save_file(E.filename); if (do_quit) exit(0); }
        else snprintf(E.statusmsg, sizeof(E.statusmsg), "E32: No file name");
        return;
    }

    if (strcmp(cmd, "wq") == 0) {
        if (E.filename[0]) { save_file(E.filename); exit(0); }
        else snprintf(E.statusmsg, sizeof(E.statusmsg), "E32: No file name");
        return;
    }

    if (strncmp(cmd, "wq ", 3) == 0) {
        char *arg = cmd + 3;
        while (*arg == ' ') arg++;
        if (*arg) {
            strncpy(E.filename, arg, sizeof(E.filename) - 1);
            E.filename[sizeof(E.filename) - 1] = '\0';
        }
        if (E.filename[0]) { save_file(E.filename); exit(0); }
        else snprintf(E.statusmsg, sizeof(E.statusmsg), "E32: No file name");
        return;
    }

    snprintf(E.statusmsg, sizeof(E.statusmsg),
             "E492: Not an editor command: %s", cmd);
}

static int yy_pending = 0;

void process_normal(int c)
{
    E.statusmsg[0] = '\0';

    if (yy_pending) {
        yy_pending = 0;
        if (c == 'y') { do_yank_line(); return; }
        snprintf(E.statusmsg, sizeof(E.statusmsg), "Unknown command");
        return;
    }

    switch (c) {
        case 'h': case KEY_LEFT:  E.cx--; clamp_cursor(); break;
        case 'l': case KEY_RIGHT: E.cx++; clamp_cursor(); break;
        case 'k': case KEY_UP:    E.cy--; clamp_cursor(); break;
        case 'j': case KEY_DOWN:  E.cy++; clamp_cursor(); break;

        case 'w': motion_w(0); break;
        case 'W': motion_w(1); break;
        case 'b': motion_b(0); break;
        case 'B': motion_b(1); break;
        case 'e': motion_e(0); break;
        case 'E': motion_e(1); break;

        case 'x':
            if (E.lines[E.cy].len > 0) {
                save_undo();
                line_delete_char(&E.lines[E.cy], E.cx);
                E.dirty = 1;
                clamp_cursor();
            }
            break;

        case 'y':
            yy_pending = 1;
            snprintf(E.statusmsg, sizeof(E.statusmsg), "y");
            break;

        case 'p':
            do_paste();
            break;

        case 'u':
            do_undo();
            break;

        case 'i':
            save_undo();
            E.mode = MODE_INSERT;
            break;

        case 'a':
            save_undo();
            if (E.cx < E.lines[E.cy].len) E.cx++;
            E.mode = MODE_INSERT;
            break;

        case 'o':
            save_undo();
            insert_line_at(E.cy + 1);
            E.cy++;
            E.cx    = 0;
            E.dirty = 1;
            E.mode  = MODE_INSERT;
            break;

        case 'O':
            save_undo();
            insert_line_at(E.cy);
            E.cx    = 0;
            E.dirty = 1;
            E.mode  = MODE_INSERT;
            break;

        case '/':
            E.mode         = MODE_SEARCH;
            E.search_len   = 0;
            E.statusmsg[0] = '\0';
            break;

        case ':':
            E.mode         = MODE_EX;
            E.ex_len       = 0;
            E.statusmsg[0] = '\0';
            break;
    }
}

void process_insert(int c)
{
    switch (c) {
        case '\x1b':
            E.mode = MODE_NORMAL;
            clamp_cursor();
            break;

        case KEY_LEFT:  E.cx--; clamp_cursor(); break;
        case KEY_RIGHT: E.cx++; clamp_cursor(); break;
        case KEY_UP:    E.cy--; clamp_cursor(); break;
        case KEY_DOWN:  E.cy++; clamp_cursor(); break;

        case 127:
        case '\b':
            if (E.cx > 0) {
                line_delete_char(&E.lines[E.cy], E.cx - 1);
                E.cx--;
                E.dirty = 1;
            } else if (E.cy > 0) {
                int prev_len = E.lines[E.cy - 1].len;
                merge_lines(E.cy - 1, E.cy);
                E.cy--;
                E.cx    = prev_len;
                E.dirty = 1;
            }
            break;

        case '\r':
        case '\n': {
            int rest = E.lines[E.cy].len - E.cx;
            insert_line_at(E.cy + 1);
            Line *newl = &E.lines[E.cy + 1];
            if (rest > 0) {
                if (rest + 1 >= newl->cap) {
                    newl->cap  = rest + LINE_INIT;
                    newl->data = realloc(newl->data, newl->cap);
                }
                memcpy(newl->data, E.lines[E.cy].data + E.cx, rest);
                newl->len          = rest;
                E.lines[E.cy].len -= rest;
            }
            E.cy++;
            E.cx    = 0;
            E.dirty = 1;
            break;
        }

        default:
            if (c >= 32 && c < 127) {
                line_insert_char(&E.lines[E.cy], E.cx++, (char)c);
                E.dirty = 1;
            }
            break;
    }
}

void process_ex_key(int c)
{
    switch (c) {
        case '\r':
        case '\n':
            process_ex_command();
            E.mode   = MODE_NORMAL;
            E.ex_len = 0;
            break;

        case '\x1b':
            E.mode         = MODE_NORMAL;
            E.ex_len       = 0;
            E.statusmsg[0] = '\0';
            break;

        case 127:
        case '\b':
            if (E.ex_len > 0) E.ex_len--;
            else { E.mode = MODE_NORMAL; E.statusmsg[0] = '\0'; }
            break;

        default:
            if (c >= 32 && c < 127 && E.ex_len < 254)
                E.ex_buf[E.ex_len++] = (char)c;
            break;
    }
}

void process_search_key(int c)
{
    switch (c) {
        case '\r':
        case '\n':
            do_search();
            E.mode = MODE_NORMAL;
            break;

        case '\x1b':
            E.mode         = MODE_NORMAL;
            E.search_len   = 0;
            E.statusmsg[0] = '\0';
            break;

        case 127:
        case '\b':
            if (E.search_len > 0) E.search_len--;
            else { E.mode = MODE_NORMAL; E.statusmsg[0] = '\0'; }
            break;

        default:
            if (c >= 32 && c < 127 && E.search_len < 254)
                E.search_buf[E.search_len++] = (char)c;
            break;
    }
}
