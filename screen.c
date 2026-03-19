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

static void ab_color(Abuf *ab, const char *name)
{
    if (!E.opt_colors) return;
    const char *v = color_get(name);
    if (v) ab_append(ab, v, (int)strlen(v));
}

static void ab_reset(Abuf *ab)
{
    if (!E.opt_colors) return;
    const char *v = color_get("normal");
    if (v) ab_append(ab, v, (int)strlen(v));
    else   ab_append(ab, "\x1b[0m", 4);
}

static int ts(void) { return E.tabstop > 0 ? E.tabstop : 8; }

int vis_col_of(int row, int byte_pos)
{
    if (row < 0 || row >= E.nlines) return byte_pos;
    Line *l  = &E.lines[row];
    int   t  = ts();
    int   vc = 0;
    for (int i = 0; i < byte_pos && i < l->len; i++) {
        if (l->data[i] == '\t')
            vc = (vc / t + 1) * t;
        else
            vc++;
    }
    return vc;
}

static void render_line(Abuf *ab, int row, int coloff_vis, int text_cols)
{
    if (row < 0 || row >= E.nlines) return;
    Line *l   = &E.lines[row];
    int   t   = ts();
    int   vc  = 0;
    int   out = 0;
    char  expanded[4096];
    int   elen = 0;

    for (int i = 0; i < l->len && out < text_cols + coloff_vis; i++) {
        if (l->data[i] == '\t') {
            int next = (vc / t + 1) * t;
            while (vc < next && elen < (int)sizeof(expanded) - 1) {
                if (vc >= coloff_vis && out < text_cols) {
                    expanded[elen++] = ' ';
                    out++;
                }
                vc++;
            }
        } else {
            if (vc >= coloff_vis && out < text_cols) {
                expanded[elen++] = l->data[i];
                out++;
            }
            vc++;
        }
    }

    if (elen > 0)
        highlight_line(ab, expanded, elen, row);
    if (E.opt_syntax || E.opt_colors)
        ab_append(ab, "\x1b[0m", 4);
}

static const char *mode_name(void)
{
    switch (E.mode) {
        case MODE_INSERT:  return "-- INSERT --";
        case MODE_REPLACE: return "-- REPLACE --";
        case MODE_SEARCH:  return "-- SEARCH --";
        case MODE_BANG:    return "-- FILTER --";
        default:           return "";
    }
}

static void pct_str(char *buf, int sz)
{
    if (E.nlines <= 1)
        snprintf(buf, sz, "All");
    else if (E.rowoff == 0)
        snprintf(buf, sz, "Top");
    else if (E.rowoff + E.rows >= E.nlines)
        snprintf(buf, sz, "Bot");
    else {
        int p = (int)((long)(E.cy + 1) * 100 / E.nlines);
        snprintf(buf, sz, "%d%%", p);
    }
}

static void format_status(char *out, int outsz, const char *fmt)
{
    int  oi = 0;
    char pct[16];
    pct_str(pct, sizeof(pct));

    for (int i = 0; fmt[i] && oi < outsz - 1; i++) {
        if (fmt[i] != '%') { out[oi++] = fmt[i]; continue; }
        i++;
        switch (fmt[i]) {
        case 'f': {
            const char *fn = E.filename[0] ? E.filename : "[No Name]";
            int l = (int)strlen(fn);
            if (oi + l >= outsz) l = outsz - oi - 1;
            memcpy(out + oi, fn, l); oi += l; break;
        }
        case 'm': {
            const char *mn = E.opt_showmode ? mode_name() : "";
            int l = (int)strlen(mn);
            if (oi + l >= outsz) l = outsz - oi - 1;
            memcpy(out + oi, mn, l); oi += l; break;
        }
        case 'M': if (oi < outsz - 1) out[oi++] = E.dirty ? '+' : ' '; break;
        case 'R': {
            const char *ro = E.readonly ? "[RO]" : "";
            int l = (int)strlen(ro);
            if (oi + l >= outsz) l = outsz - oi - 1;
            memcpy(out + oi, ro, l); oi += l; break;
        }
        case 'l': {
            char tmp[16]; int l = snprintf(tmp, sizeof(tmp), "%d", E.cy + 1);
            if (oi + l >= outsz) l = outsz - oi - 1;
            memcpy(out + oi, tmp, l); oi += l; break;
        }
        case 'L': {
            char tmp[16]; int l = snprintf(tmp, sizeof(tmp), "%d", E.nlines);
            if (oi + l >= outsz) l = outsz - oi - 1;
            memcpy(out + oi, tmp, l); oi += l; break;
        }
        case 'c': {
            char tmp[16]; int l = snprintf(tmp, sizeof(tmp), "%d", E.cx + 1);
            if (oi + l >= outsz) l = outsz - oi - 1;
            memcpy(out + oi, tmp, l); oi += l; break;
        }
        case 'p': {
            int l = (int)strlen(pct);
            if (oi + l >= outsz) l = outsz - oi - 1;
            memcpy(out + oi, pct, l); oi += l; break;
        }
        case 't': {
            int l = (int)strlen(E.filetype);
            if (oi + l >= outsz) l = outsz - oi - 1;
            memcpy(out + oi, E.filetype, l); oi += l; break;
        }
        case '%': if (oi < outsz - 1) out[oi++] = '%'; break;
        default:
            if (oi < outsz - 2) { out[oi++] = '%'; out[oi++] = fmt[i]; } break;
        }
    }
    out[oi] = '\0';
}

static void build_status_bar(Abuf *ab)
{
    char left[256], right[128];
    int  llen, rlen;

    if (E.opt_colors) {
        const char *sb = color_get("statusbar");
        if (sb) ab_append(ab, sb, (int)strlen(sb));
        else    ab_append(ab, "\x1b[7m", 4);
    } else {
        ab_append(ab, "\x1b[7m", 4);
    }

    if (E.opt_statusfmt[0]) {
        format_status(left, sizeof(left), E.opt_statusfmt);
        llen = (int)strlen(left); rlen = 0; right[0] = '\0';
    } else {
        const char *mn    = E.opt_showmode ? mode_name() : "";
        const char *fname = E.filename[0]  ? E.filename  : "[No Name]";
        const char *ronly = E.readonly      ? " [RO]"     : "";
        char pct[16];
        pct_str(pct, sizeof(pct));
        llen = snprintf(left, sizeof(left), " %-13s %.36s%s %c",
                        mn, fname, ronly, E.dirty ? '+' : ' ');
        if (llen < 0) llen = 0;
        if (E.opt_ruler) {
            rlen = snprintf(right, sizeof(right), "%s  %d,%d",
                            pct, E.cy + 1, E.cx + 1);
        } else {
            rlen = 0; right[0] = '\0';
        }
        if (rlen < 0) rlen = 0;
    }

    int pad = E.cols - llen - rlen;
    if (llen > E.cols) llen = E.cols;
    ab_append(ab, left, llen);

    int written = llen;
    if (pad > 0 && written < E.cols) {
        int sp = pad < (E.cols - written) ? pad : (E.cols - written);
        char spaces[256];
        if (sp > 255) sp = 255;
        memset(spaces, ' ', sp);
        ab_append(ab, spaces, sp); written += sp;
    }
    if (rlen > 0 && written < E.cols) {
        int avail = E.cols - written;
        int r     = rlen < avail ? rlen : avail;
        ab_append(ab, right + (rlen - r), r); written += r;
    }
    while (written < E.cols) { ab_append(ab, " ", 1); written++; }

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
        ab_append(ab, E.search_dir > 0 ? "/" : "?", 1);
        if (E.search_len > 0) {
            int show = E.search_len < E.cols - 1 ? E.search_len : E.cols - 1;
            ab_append(ab, E.search_buf, show);
        }
    } else if (E.mode == MODE_BANG) {
        ab_append(ab, "!", 1);
        if (E.bang_len > 0) {
            int show = E.bang_len < E.cols - 1 ? E.bang_len : E.cols - 1;
            ab_append(ab, E.bang_buf, show);
        }
    } else if (E.statusmsg[0]) {
        int ml   = (int)strlen(E.statusmsg);
        int show = ml < E.cols ? ml : E.cols;
        if (E.opt_colors && E.statusmsg_err) {
            const char *ec = color_get("error");
            if (ec) ab_append(ab, ec, (int)strlen(ec));
            else    ab_append(ab, "\x1b[41;1;37m", 10);
        }
        ab_append(ab, E.statusmsg, show);
        if (E.opt_colors && E.statusmsg_err)
            ab_append(ab, "\x1b[0m", 4);
    }
}

void draw_screen(void)
{
    int numw = 0;
    if (E.opt_number) {
        char tmp[16];
        numw = snprintf(tmp, sizeof(tmp), "%d", E.nlines);
        if (numw < 1) numw = 1;
        numw += 1;
    }

    int text_cols = E.cols - numw;
    if (text_cols < 1) text_cols = 1;

    int vis_cx = vis_col_of(E.cy, E.cx);

    if (E.cy < E.rowoff)                    E.rowoff = E.cy;
    if (E.cy >= E.rowoff + E.rows)          E.rowoff = E.cy - E.rows + 1;
    if (vis_cx < E.coloff)                  E.coloff = vis_cx;
    if (vis_cx >= E.coloff + text_cols)     E.coloff = vis_cx - text_cols + 1;
    if (E.rowoff < 0)                       E.rowoff = 0;
    if (E.coloff < 0)                       E.coloff = 0;

    Abuf ab = {NULL, 0};
    char buf[64];
    int  n;

    ab_append(&ab, "\x1b[?25l", 6);
    ab_append(&ab, "\x1b[H",    3);

    for (int y = 0; y < E.rows; y++) {
        int filerow = y + E.rowoff;

        if (filerow >= E.nlines) {
            if (E.opt_number) {
                char pad[32];
                int  pl = snprintf(pad, sizeof(pad), "%-*s", numw, "~");
                ab_color(&ab, "tilde");
                ab_append(&ab, pad, pl);
                ab_reset(&ab);
            } else {
                ab_color(&ab, "tilde");
                ab_append(&ab, "~", 1);
                ab_reset(&ab);
            }
            ab_append(&ab, "\x1b[K\r\n", 5);
            continue;
        }

        if (E.opt_number) {
            char numstr[32];
            int  nl = snprintf(numstr, sizeof(numstr), "%*d ",
                                numw - 1, filerow + 1);
            if (filerow == E.cy)
                ab_color(&ab, "linenr_cur");
            else
                ab_color(&ab, "linenr");
            ab_append(&ab, numstr, nl);
            ab_reset(&ab);
        }

        render_line(&ab, filerow, E.coloff, text_cols);
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
    } else if (E.mode == MODE_BANG) {
        int col = E.bang_len + 2;
        if (col > E.cols) col = E.cols;
        n = snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.rows + 2, col);
    } else {
        int scr_row = E.cy - E.rowoff + 1;
        int scr_col = vis_cx - E.coloff + 1 + numw;
        if (scr_col < 1) scr_col = 1;
        n = snprintf(buf, sizeof(buf), "\x1b[%d;%dH", scr_row, scr_col);
    }
    ab_append(&ab, buf, n);
    ab_append(&ab, "\x1b[?25h", 6);

    write(STDOUT_FILENO, ab.b, ab.len);
    ab_free(&ab);
}
