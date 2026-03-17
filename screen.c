#include "vi.h"

void ab_append(Abuf *ab, const char *s, int len)
{
    ab->b = realloc(ab->b, ab->len + len);
    memcpy(ab->b + ab->len, s, len);
    ab->len += len;
}

void ab_free(Abuf *ab)
{
    free(ab->b);
    ab->b   = NULL;
    ab->len = 0;
}

static void build_status_bar(Abuf *ab)
{
    char left[128], right[128];
    int  llen, rlen;

    const char *mname = "";
    if (E.mode == MODE_INSERT) mname = "-- INSERT --";
    if (E.mode == MODE_SEARCH) mname = "-- SEARCH --";

    const char *fname = E.filename[0] ? E.filename : "[No Name]";
    char dirty_flag   = E.dirty ? '+' : ' ';

    llen = snprintf(left, sizeof(left), " %-12s  %.40s %c",
                    mname, fname, dirty_flag);
    if (llen < 0) llen = 0;

    char pct[16];
    if (E.nlines <= 1) {
        snprintf(pct, sizeof(pct), "All");
    } else if (E.rowoff == 0) {
        snprintf(pct, sizeof(pct), "Top");
    } else if (E.rowoff + E.rows >= E.nlines) {
        snprintf(pct, sizeof(pct), "Bot");
    } else {
        int p = (int)((long)(E.cy + 1) * 100 / E.nlines);
        snprintf(pct, sizeof(pct), "%d%%", p);
    }

    rlen = snprintf(right, sizeof(right), "%s  %d,%d",
                    pct, E.cy + 1, E.cx + 1);
    if (rlen < 0) rlen = 0;

    int total = llen + rlen;
    int pad   = E.cols - total;

    ab_append(ab, "\x1b[7m", 4);

    if (llen > E.cols) llen = E.cols;
    ab_append(ab, left, llen);

    int written = llen;
    if (pad > 0 && written < E.cols) {
        int sp = pad < (E.cols - written) ? pad : (E.cols - written);
        char spaces[256];
        if (sp > 255) sp = 255;
        memset(spaces, ' ', sp);
        ab_append(ab, spaces, sp);
        written += sp;
    }

    if (rlen > 0 && written < E.cols) {
        int avail = E.cols - written;
        int r     = rlen < avail ? rlen : avail;
        ab_append(ab, right + (rlen - r), r);
        written += r;
    }

    while (written < E.cols) {
        ab_append(ab, " ", 1);
        written++;
    }

    ab_append(ab, "\x1b[m", 3);
}

static void build_command_line(Abuf *ab)
{
    ab_append(ab, "\x1b[K", 3);

    if (E.mode == MODE_EX) {
        ab_append(ab, ":", 1);
        if (E.ex_len > 0) {
            int show = E.ex_len < E.cols - 1 ? E.ex_len : E.cols - 1;
            ab_append(ab, E.ex_buf, show);
        }
    } else if (E.mode == MODE_SEARCH) {
        ab_append(ab, "/", 1);
        if (E.search_len > 0) {
            int show = E.search_len < E.cols - 1 ? E.search_len : E.cols - 1;
            ab_append(ab, E.search_buf, show);
        }
    } else if (E.statusmsg[0]) {
        int ml = (int)strlen(E.statusmsg);
        int show = ml < E.cols ? ml : E.cols;
        ab_append(ab, E.statusmsg, show);
    }
}

void draw_screen(void)
{
    if (E.cy < E.rowoff)               E.rowoff = E.cy;
    if (E.cy >= E.rowoff + E.rows)     E.rowoff = E.cy - E.rows + 1;
    if (E.cx < E.coloff)               E.coloff = E.cx;
    if (E.cx >= E.coloff + E.cols)     E.coloff = E.cx - E.cols + 1;

    Abuf ab = {NULL, 0};
    char buf[64];
    int  n;

    ab_append(&ab, "\x1b[?25l", 6);
    ab_append(&ab, "\x1b[H",    3);

    for (int y = 0; y < E.rows; y++) {
        int filerow = y + E.rowoff;

        if (filerow >= E.nlines) {
            ab_append(&ab, "~\x1b[K\r\n", 7);
            continue;
        }

        Line *l   = &E.lines[filerow];
        int   col = E.coloff < l->len ? E.coloff : l->len;
        int   len = l->len - col;
        if (len < 0) len = 0;
        if (len > E.cols) len = E.cols;

        if (len > 0) ab_append(&ab, l->data + col, len);
        ab_append(&ab, "\x1b[K\r\n", 5);
    }

    build_status_bar(&ab);
    ab_append(&ab, "\r\n", 2);
    build_command_line(&ab);

    if (E.mode == MODE_EX) {
        int col = E.ex_len + 2;
        if (col > E.cols) col = E.cols;
        n = snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.rows + 2, col);
    } else if (E.mode == MODE_SEARCH) {
        int col = E.search_len + 2;
        if (col > E.cols) col = E.cols;
        n = snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.rows + 2, col);
    } else {
        int scr_row = E.cy - E.rowoff + 1;
        int scr_col = E.cx - E.coloff + 1;
        n = snprintf(buf, sizeof(buf), "\x1b[%d;%dH", scr_row, scr_col);
    }
    ab_append(&ab, buf, n);
    ab_append(&ab, "\x1b[?25h", 6);

    write(STDOUT_FILENO, ab.b, ab.len);
    ab_free(&ab);
}
