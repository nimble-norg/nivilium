#include "vi.h"

void clamp_cursor(void)
{
    if (E.cy < 0)         E.cy = 0;
    if (E.cy >= E.nlines) E.cy = E.nlines - 1;

    int llen = E.lines[E.cy].len;

    if (E.mode == MODE_INSERT || E.mode == MODE_REPLACE) {
        if (E.cx > llen) E.cx = llen;
    } else {
        if (E.cx >= llen) E.cx = llen > 0 ? llen - 1 : 0;
    }
    if (E.cx < 0) E.cx = 0;
}

static int is_word(char c)  { return isalnum((unsigned char)c) || c == '_'; }
static int is_blank(char c) { return c == ' ' || c == '\t'; }

static void scroll_to_center(void)
{
    E.rowoff = E.cy - E.rows / 2;
    if (E.rowoff < 0) E.rowoff = 0;
}

static void scroll_to_top(void)    { E.rowoff = E.cy; }

static void scroll_to_bottom(void)
{
    E.rowoff = E.cy - E.rows + 1;
    if (E.rowoff < 0) E.rowoff = 0;
}

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
        row++; d = E.lines[row].data; l = E.lines[row].len; col = 0;
        while (col < l && is_blank(d[col])) col++;
        E.cy = row; E.cx = col < l ? col : 0;
    }
}

static void motion_b(int big)
{
    int row = E.cy, col = E.cx;
    if (col == 0) {
        if (row == 0) return;
        row--; col = E.lines[row].len > 0 ? E.lines[row].len - 1 : 0;
        E.cy = row; E.cx = col; return;
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
            row++; d = E.lines[row].data; l = E.lines[row].len; col = 0;
        } else return;
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
    E.cy = row; E.cx = col < l ? col : (l > 0 ? l - 1 : 0);
}

static int first_nonblank(int row)
{
    Line *l = &E.lines[row];
    int   i = 0;
    while (i < l->len && is_blank(l->data[i])) i++;
    return i;
}

static int leading_ws_len(int row)
{
    return first_nonblank(row);
}

static void buf_yank(const char *data, int len)
{
    int pb = E.pending_buf;
    E.pending_buf = -1;
    if (pb >= 'a' && pb <= 'z') {
        int idx = pb - 'a';
        free(E.nbuf_data[idx]);
        E.nbuf_data[idx] = malloc(len + 1);
        memcpy(E.nbuf_data[idx], data, len);
        E.nbuf_len[idx]  = len;
    } else if (pb >= 'A' && pb <= 'Z') {
        int idx    = pb - 'A';
        int newlen = E.nbuf_len[idx] + len;
        char *nd   = malloc(newlen + 1);
        if (E.nbuf_data[idx]) memcpy(nd, E.nbuf_data[idx], E.nbuf_len[idx]);
        memcpy(nd + E.nbuf_len[idx], data, len);
        free(E.nbuf_data[idx]);
        E.nbuf_data[idx] = nd; E.nbuf_len[idx] = newlen;
    } else {
        free(E.yank_data);
        E.yank_data = malloc(len + 1);
        memcpy(E.yank_data, data, len);
        E.yank_len  = len;
    }
}

static void buf_get(char **data_out, int *len_out)
{
    int pb = E.pending_buf;
    E.pending_buf = -1;
    if (pb >= 'a' && pb <= 'z') {
        *data_out = E.nbuf_data[pb - 'a'];
        *len_out  = E.nbuf_len[pb - 'a'];
    } else if (pb >= 'A' && pb <= 'Z') {
        *data_out = E.nbuf_data[pb - 'A'];
        *len_out  = E.nbuf_len[pb - 'A'];
    } else {
        *data_out = E.yank_data;
        *len_out  = E.yank_len;
    }
}

static void do_yank_lines(int cnt)
{
    if (cnt < 1) cnt = 1;
    int end = E.cy + cnt - 1;
    if (end >= E.nlines) end = E.nlines - 1;
    int total_len = 0;
    for (int i = E.cy; i <= end; i++) total_len += E.lines[i].len + 1;
    char *buf = malloc(total_len + 1);
    int   pos = 0;
    for (int i = E.cy; i <= end; i++) {
        memcpy(buf + pos, E.lines[i].data, E.lines[i].len);
        pos += E.lines[i].len;
        if (i < end) buf[pos++] = '\n';
    }
    buf_yank(buf, pos);
    free(buf);
    int n = end - E.cy + 1;
    snprintf(E.statusmsg, sizeof(E.statusmsg),
             "%d line%s yanked", n, n > 1 ? "s" : "");
}

static void do_delete_lines(int cnt)
{
    if (E.nlines == 0) return;
    save_undo();
    if (cnt < 1) cnt = 1;
    int end = E.cy + cnt - 1;
    if (end >= E.nlines) end = E.nlines - 1;
    int n = end - E.cy + 1;
    int total_len = 0;
    for (int i = E.cy; i <= end; i++) total_len += E.lines[i].len + 1;
    char *buf = malloc(total_len + 1);
    int   pos = 0;
    for (int i = E.cy; i <= end; i++) {
        memcpy(buf + pos, E.lines[i].data, E.lines[i].len);
        pos += E.lines[i].len;
        if (i < end) buf[pos++] = '\n';
    }
    buf_yank(buf, pos);
    free(buf);
    for (int k = 0; k < n; k++) {
        if (E.cy < E.nlines) {
            free(E.lines[E.cy].data);
            memmove(&E.lines[E.cy], &E.lines[E.cy + 1],
                    sizeof(Line) * (E.nlines - E.cy - 1));
            E.nlines--;
        }
    }
    if (E.nlines == 0) insert_line_at(0);
    if (E.cy >= E.nlines) E.cy = E.nlines - 1;
    E.dirty = 1; clamp_cursor();
    snprintf(E.statusmsg, sizeof(E.statusmsg),
             "%d line%s deleted", n, n > 1 ? "s" : "");
}

static void do_paste(int before)
{
    char *data; int len;
    buf_get(&data, &len);
    if (!data) return;
    save_undo();
    int ins_at = before ? E.cy : E.cy + 1;
    insert_line_at(ins_at);
    if (!before) E.cy++;
    Line *l = &E.lines[E.cy];
    if (len + 1 > l->cap) {
        l->cap  = len + LINE_INIT;
        l->data = realloc(l->data, l->cap);
    }
    memcpy(l->data, data, len);
    l->len = len; E.cx = 0; E.dirty = 1;
}

static void do_search(int dir)
{
    if (E.search_len <= 0) return;
    int slen = E.search_len;
    if (dir > 0) {
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
    } else {
        for (int i = E.cy; i >= 0; i--) {
            Line *l     = &E.lines[i];
            int   start = (i == E.cy) ? E.cx - 1 : l->len - slen;
            for (int j = start; j >= 0; j--) {
                if (memcmp(l->data + j, E.search_buf, slen) == 0) {
                    E.cy = i; E.cx = j;
                    snprintf(E.statusmsg, sizeof(E.statusmsg),
                             "?%.*s", slen, E.search_buf);
                    return;
                }
            }
        }
        for (int i = E.nlines - 1; i >= E.cy; i--) {
            Line *l     = &E.lines[i];
            int   start = (i == E.cy) ? E.cx : l->len - slen;
            for (int j = start; j >= 0; j--) {
                if (memcmp(l->data + j, E.search_buf, slen) == 0) {
                    E.cy = i; E.cx = j;
                    snprintf(E.statusmsg, sizeof(E.statusmsg),
                             "search wrapped: ?%.*s", slen, E.search_buf);
                    return;
                }
            }
        }
    }
    snprintf(E.statusmsg, sizeof(E.statusmsg),
             "E486: Pattern not found: %.*s", slen, E.search_buf);
}

static void dispatch_rhs(const char *rhs, int for_insert);

static void handle_wait_state(int c)
{
    WaitState ws   = E.wait_state;
    int        cnt = E.wait_count > 0 ? E.wait_count : 1;
    E.wait_state   = WAIT_NONE;
    E.wait_count   = 0;
    E.statusmsg[0] = '\0';

    switch (ws) {
    case WAIT_BUFFER:
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))
            E.pending_buf = c;
        else
            snprintf(E.statusmsg, sizeof(E.statusmsg), "Unknown buffer");
        break;

    case WAIT_YY:
        if (c == 'y') do_yank_lines(cnt);
        else snprintf(E.statusmsg, sizeof(E.statusmsg), "Unknown command");
        break;

    case WAIT_DD:
        if (c == 'd') do_delete_lines(cnt);
        else snprintf(E.statusmsg, sizeof(E.statusmsg), "Unknown command");
        break;

    case WAIT_ZZ:
        if (c == 'Z') {
            if (E.filename[0]) {
                if (E.dirty) save_file(E.filename);
                if (!E.dirty) clean_exit_editor();
            } else {
                if (!E.dirty) clean_exit_editor();
                snprintf(E.statusmsg, sizeof(E.statusmsg), "E32: No file name");
            }
        } else {
            snprintf(E.statusmsg, sizeof(E.statusmsg), "Unknown command");
        }
        break;

    case WAIT_Z:
        switch (c) {
        case 'z': case '.': scroll_to_center(); break;
        case 't': case '\r': case '\n': scroll_to_top(); break;
        case 'b': case '-': scroll_to_bottom(); break;
        default: break;
        }
        break;

    case WAIT_GG:
        if (c == 'g') {
            E.cy = cnt > 1 ? cnt - 1 : 0;
            if (E.cy >= E.nlines) E.cy = E.nlines - 1;
            clamp_cursor(); E.cx = first_nonblank(E.cy);
        } else if (c == 'G') {
            E.cy = E.nlines > 0 ? E.nlines - 1 : 0;
            clamp_cursor(); E.cx = first_nonblank(E.cy);
        } else {
            snprintf(E.statusmsg, sizeof(E.statusmsg), "Unknown command");
        }
        break;

    case WAIT_REPLACE_CHAR:
        if (c >= 32 && c < 127) {
            save_undo();
            for (int i = 0; i < cnt; i++) {
                int pos = E.cx + i;
                if (pos < E.lines[E.cy].len)
                    E.lines[E.cy].data[pos] = (char)c;
            }
            E.dirty = 1;
        }
        break;

    case WAIT_BANG_MOTION: {
        int line1 = E.cy, line2 = E.cy;
        if (c == '!') {
            line2 = E.cy + cnt - 1;
            if (line2 >= E.nlines) line2 = E.nlines - 1;
        } else if (c == 'j' || c == KEY_DOWN) {
            line2 = E.cy + cnt;
            if (line2 >= E.nlines) line2 = E.nlines - 1;
        } else if (c == 'k' || c == KEY_UP) {
            line1 = E.cy - cnt;
            if (line1 < 0) line1 = 0;
        } else if (c == 'G') {
            line2 = E.nlines - 1;
        } else if (c == 'H') {
            line1 = E.rowoff;
        } else if (c == 'L') {
            line2 = E.rowoff + E.rows - 1;
            if (line2 >= E.nlines) line2 = E.nlines - 1;
        } else {
            snprintf(E.statusmsg, sizeof(E.statusmsg), "Unknown motion");
            break;
        }
        if (line1 > line2) { int t = line1; line1 = line2; line2 = t; }
        E.bang_line1 = line1; E.bang_line2 = line2;
        E.bang_len = 0; E.bang_buf[0] = '\0';
        E.mode = MODE_BANG;
        snprintf(E.statusmsg, sizeof(E.statusmsg), "!");
        break;
    }

    default: break;
    }
}

static void do_normal_key(int c);

static void dispatch_rhs(const char *rhs, int for_insert)
{
    (void)for_insert;
    while (*rhs) {
        unsigned char ch = (unsigned char)*rhs;
        int code;
        if (ch == '\x01' && rhs[1]) {
            code = ((unsigned char)rhs[1] << 8) | (unsigned char)rhs[2];
            rhs += 3;
        } else {
            code = ch;
            rhs++;
        }
        if (E.mode == MODE_NORMAL)
            do_normal_key(code);
        else if (E.mode == MODE_INSERT || E.mode == MODE_REPLACE)
            process_insert(code);
    }
}

static void do_normal_key(int c)
{
    if (E.wait_state != WAIT_NONE) {
        handle_wait_state(c);
        return;
    }

    if (c >= '1' && c <= '9' && !E.count_started) {
        E.count = c - '0'; E.count_started = 1; return;
    }
    if (c >= '0' && c <= '9' && E.count_started) {
        E.count = E.count * 10 + (c - '0'); return;
    }

    int cnt = E.count_started ? (E.count > 0 ? E.count : 1) : 1;
    E.count = 0; E.count_started = 0; E.statusmsg[0] = '\0';

    char seq[4];
    if (c >= KEY_UP) {
        seq[0] = '\x01';
        seq[1] = (char)((c >> 8) & 0xff);
        seq[2] = (char)(c & 0xff);
        seq[3] = '\0';
    } else {
        seq[0] = (char)c;
        seq[1] = '\0';
    }
    const char *mapped = map_lookup(seq, 0);
    if (mapped) { dispatch_rhs(mapped, 0); return; }

    switch (c) {
    case '"': E.wait_state = WAIT_BUFFER; break;

    case 'h': case KEY_LEFT:
        for (int i = 0; i < cnt; i++) E.cx--;
        clamp_cursor(); break;
    case 'l': case KEY_RIGHT: case ' ':
        for (int i = 0; i < cnt; i++) E.cx++;
        clamp_cursor(); break;
    case 'k': case KEY_UP:
        for (int i = 0; i < cnt; i++) E.cy--;
        clamp_cursor(); break;
    case 'j': case KEY_DOWN:
        for (int i = 0; i < cnt; i++) E.cy++;
        clamp_cursor(); break;

    case '0': case KEY_HOME: E.cx = 0; break;
    case '^':  E.cx = first_nonblank(E.cy); break;
    case '$': case KEY_END:
        if (cnt > 1) {
            int row = E.cy + cnt - 1;
            if (row >= E.nlines) row = E.nlines - 1;
            E.cy = row;
        }
        E.cx = E.lines[E.cy].len > 0 ? E.lines[E.cy].len - 1 : 0;
        break;

    case 'G':
        E.cy = E.count_started ? cnt - 1 : E.nlines - 1;
        if (E.cy >= E.nlines) E.cy = E.nlines - 1;
        clamp_cursor(); E.cx = first_nonblank(E.cy); break;
    case 'H': {
        int row = E.rowoff + cnt - 1;
        if (row >= E.nlines) row = E.nlines - 1;
        E.cy = row; clamp_cursor(); break;
    }
    case 'M':
        E.cy = E.rowoff + E.rows / 2;
        if (E.cy >= E.nlines) E.cy = E.nlines - 1;
        clamp_cursor(); break;
    case 'L': {
        int row = E.rowoff + E.rows - cnt;
        if (row < 0) row = 0;
        if (row >= E.nlines) row = E.nlines - 1;
        E.cy = row; clamp_cursor(); break;
    }

    case 'w': for (int i = 0; i < cnt; i++) motion_w(0); break;
    case 'W': for (int i = 0; i < cnt; i++) motion_w(1); break;
    case 'b': for (int i = 0; i < cnt; i++) motion_b(0); break;
    case 'B': for (int i = 0; i < cnt; i++) motion_b(1); break;
    case 'e': for (int i = 0; i < cnt; i++) motion_e(0); break;
    case 'E': for (int i = 0; i < cnt; i++) motion_e(1); break;

    case 'x': {
        if (E.lines[E.cy].len > 0) {
            save_undo();
            for (int i = 0; i < cnt; i++) {
                if (E.cx < E.lines[E.cy].len)
                    line_delete_char(&E.lines[E.cy], E.cx);
            }
            E.dirty = 1; clamp_cursor();
        }
        break;
    }
    case 'X': {
        save_undo();
        for (int i = 0; i < cnt; i++) {
            if (E.cx > 0) {
                E.cx--; line_delete_char(&E.lines[E.cy], E.cx); E.dirty = 1;
            }
        }
        clamp_cursor(); break;
    }
    case 'D': {
        if (E.lines[E.cy].len > 0) {
            save_undo();
            buf_yank(E.lines[E.cy].data + E.cx, E.lines[E.cy].len - E.cx);
            E.lines[E.cy].len = E.cx; E.dirty = 1; clamp_cursor();
        }
        break;
    }
    case 'C': {
        save_undo();
        buf_yank(E.lines[E.cy].data + E.cx, E.lines[E.cy].len - E.cx);
        E.lines[E.cy].len = E.cx;
        E.dirty = 1; E.mode = MODE_INSERT; E.ins_current_len = 0; break;
    }
    case 's': {
        save_undo();
        for (int i = 0; i < cnt; i++) {
            if (E.cx < E.lines[E.cy].len)
                line_delete_char(&E.lines[E.cy], E.cx);
        }
        E.dirty = 1; E.mode = MODE_INSERT; E.ins_current_len = 0; break;
    }
    case 'S': {
        save_undo(); E.cx = 0; E.lines[E.cy].len = 0;
        E.dirty = 1; E.mode = MODE_INSERT; E.ins_current_len = 0; break;
    }

    case 'y': E.wait_state = WAIT_YY; E.wait_count = cnt;
              snprintf(E.statusmsg, sizeof(E.statusmsg), "y"); break;
    case 'Y': do_yank_lines(cnt); break;
    case 'p': do_paste(0); break;
    case 'P': do_paste(1); break;
    case 'd': E.wait_state = WAIT_DD; E.wait_count = cnt; break;
    case 'u': do_undo(); break;

    case 'r': E.wait_state = WAIT_REPLACE_CHAR; E.wait_count = cnt; break;
    case 'R':
        save_undo(); E.mode = MODE_REPLACE; E.ins_current_len = 0; break;

    case '~': {
        save_undo();
        for (int i = 0; i < cnt; i++) {
            if (E.cx < E.lines[E.cy].len) {
                char ch = E.lines[E.cy].data[E.cx];
                if (isupper((unsigned char)ch))
                    E.lines[E.cy].data[E.cx] = (char)tolower((unsigned char)ch);
                else if (islower((unsigned char)ch))
                    E.lines[E.cy].data[E.cx] = (char)toupper((unsigned char)ch);
                E.cx++;
            }
        }
        E.dirty = 1; clamp_cursor(); break;
    }

    case 'J': {
        save_undo();
        for (int i = 0; i < cnt; i++) {
            if (E.cy + 1 < E.nlines) {
                int prev_len = E.lines[E.cy].len;
                if (prev_len > 0 && E.lines[E.cy + 1].len > 0)
                    line_insert_char(&E.lines[E.cy], prev_len, ' ');
                merge_lines(E.cy, E.cy + 1);
            }
        }
        E.dirty = 1; clamp_cursor(); break;
    }

    case 'Z': E.wait_state = WAIT_ZZ; E.wait_count = 1; break;

    case 'Q':
        disable_raw_mode();
        write(STDOUT_FILENO, "\x1b[2J", 4);
        write(STDOUT_FILENO, "\x1b[H",  3);
        E.ex_mode = 1;
        run_ex_mode();
        if (!E.ex_mode) { reenable_raw_mode(); get_window_size(); }
        break;

    case 'n': do_search(E.search_dir);  break;
    case 'N': do_search(-E.search_dir); break;

    case '!': E.wait_state = WAIT_BANG_MOTION; E.wait_count = cnt;
              snprintf(E.statusmsg, sizeof(E.statusmsg), "!"); break;

    case 'i':
        save_undo(); E.mode = MODE_INSERT; E.ins_current_len = 0; break;
    case 'I':
        save_undo(); E.cx = first_nonblank(E.cy);
        E.mode = MODE_INSERT; E.ins_current_len = 0; break;
    case 'a':
        save_undo();
        if (E.cx < E.lines[E.cy].len) E.cx++;
        E.mode = MODE_INSERT; E.ins_current_len = 0; break;
    case 'A':
        save_undo(); E.cx = E.lines[E.cy].len;
        E.mode = MODE_INSERT; E.ins_current_len = 0; break;
    case 'o':
        save_undo(); insert_line_at(E.cy + 1); E.cy++;
        E.cx = 0; E.dirty = 1; E.mode = MODE_INSERT; E.ins_current_len = 0; break;
    case 'O':
        save_undo(); insert_line_at(E.cy);
        E.cx = 0; E.dirty = 1; E.mode = MODE_INSERT; E.ins_current_len = 0; break;

    case '/': E.mode = MODE_SEARCH; E.search_len = 0; E.search_dir = 1;  break;
    case '?': E.mode = MODE_SEARCH; E.search_len = 0; E.search_dir = -1; break;
    case ':': E.mode = MODE_EX;     E.ex_len     = 0; break;

    case 'g': E.wait_state = WAIT_GG; E.wait_count = cnt; break;
    case 'z': E.wait_state = WAIT_Z;  E.wait_count = cnt; break;

    case 0x02: case 0x06: {
        int half = (c == 0x02) ? -(E.rows * cnt) : (E.rows * cnt);
        E.cy += half; clamp_cursor();
        E.rowoff = E.cy - E.rows / 2;
        if (E.rowoff < 0) E.rowoff = 0;
        break;
    }
    case 0x04: {
        int step = E.rows / 2;
        if (step < 1) step = 1;
        E.cy += step * cnt; clamp_cursor(); break;
    }
    case 0x15: {
        int step = E.rows / 2;
        if (step < 1) step = 1;
        E.cy -= step * cnt; clamp_cursor(); break;
    }
    case 0x05:
        E.rowoff += cnt;
        if (E.rowoff >= E.nlines) E.rowoff = E.nlines - 1;
        if (E.cy < E.rowoff) E.cy = E.rowoff;
        clamp_cursor(); break;
    case 0x19:
        E.rowoff -= cnt;
        if (E.rowoff < 0) E.rowoff = 0;
        if (E.cy >= E.rowoff + E.rows) E.cy = E.rowoff + E.rows - 1;
        clamp_cursor(); break;
    case 0x0c: case 0x12: draw_screen(); break;
    case 0x07: {
        const char *fname = E.filename[0] ? E.filename : "[No Name]";
        int p = E.nlines > 0 ? (int)((long)(E.cy + 1) * 100 / E.nlines) : 0;
        snprintf(E.statusmsg, sizeof(E.statusmsg),
                 "\"%s\"%s  %d/%dL  %d%%",
                 fname, E.dirty ? " [Modified]" : "", E.cy + 1, E.nlines, p);
        break;
    }
    case 0x1a:
        disable_raw_mode(); kill(getpid(), SIGTSTP); break;
    case '+': case 0x0d:
        for (int i = 0; i < cnt; i++) E.cy++;
        clamp_cursor(); E.cx = first_nonblank(E.cy); break;
    case '-':
        for (int i = 0; i < cnt; i++) E.cy--;
        clamp_cursor(); E.cx = first_nonblank(E.cy); break;
    case '|':
        E.cx = cnt - 1; clamp_cursor(); break;
    case KEY_PPAGE: {
        int step = E.rows * cnt;
        E.cy -= step; clamp_cursor(); E.rowoff = E.cy;
        if (E.rowoff < 0) E.rowoff = 0;
        break;
    }
    case KEY_NPAGE: {
        int step = E.rows * cnt;
        E.cy += step; clamp_cursor();
        E.rowoff = E.cy - E.rows / 2;
        if (E.rowoff < 0) E.rowoff = 0;
        break;
    }
    case KEY_DEL: {
        if (E.lines[E.cy].len > 0) {
            save_undo(); line_delete_char(&E.lines[E.cy], E.cx);
            E.dirty = 1; clamp_cursor();
        }
        break;
    }
    default: break;
    }
}

void process_normal(int c)
{
    do_normal_key(c);
}

static void ins_record(int c)
{
    if (E.ins_current_len < (int)sizeof(E.ins_current) - 1)
        E.ins_current[E.ins_current_len++] = (char)c;
}

static int col_of_cx(void)
{
    int   t   = E.tabstop > 0 ? E.tabstop : 8;
    int   col = 0;
    Line *l   = &E.lines[E.cy];
    for (int i = 0; i < E.cx && i < l->len; i++) {
        if (l->data[i] == '\t')
            col = (col / t + 1) * t;
        else
            col++;
    }
    return col;
}

static void ins_ctrl_d(void)
{
    int   t   = E.tabstop > 0 ? E.tabstop : 8;
    int   sw  = E.shiftwidth > 0 ? E.shiftwidth : t;
    Line *l   = &E.lines[E.cy];
    int   col = col_of_cx();
    if (col == 0) return;
    int tgt = ((col - 1) / sw) * sw;
    if (tgt < 0) tgt = 0;
    while (E.cx > 0 && col_of_cx() > tgt) {
        E.cx--;
        line_delete_char(l, E.cx);
    }
    E.dirty = 1;
}

static void ins_ctrl_t(void)
{
    int  sw  = E.shiftwidth > 0 ? E.shiftwidth : 4;
    int  col = col_of_cx();
    int  tgt = (col / sw + 1) * sw;
    char ic  = E.opt_indentchar ? E.opt_indentchar : '\t';

    if (ic == '\t') {
        line_insert_char(&E.lines[E.cy], E.cx, '\t');
        E.cx++;
    } else {
        int needed = tgt - col_of_cx();
        for (int i = 0; i < needed; i++) {
            line_insert_char(&E.lines[E.cy], E.cx, ic);
            E.cx++;
        }
    }
    E.dirty = 1;
}

static void ins_ctrl_w(void)
{
    Line *l = &E.lines[E.cy];
    if (E.cx == 0) return;
    while (E.cx > 0 && is_blank(l->data[E.cx - 1])) { E.cx--; line_delete_char(l, E.cx); }
    while (E.cx > 0 && !is_blank(l->data[E.cx - 1])) { E.cx--; line_delete_char(l, E.cx); }
    E.dirty = 1;
}

static void ins_erase_line(void)
{
    Line *l = &E.lines[E.cy];
    while (E.cx > 0) { E.cx--; line_delete_char(l, E.cx); }
    E.dirty = 1;
}

static void ins_replay_prev(void)
{
    if (E.ins_replay_len <= 0) return;
    for (int i = 0; i < E.ins_replay_len; i++) {
        unsigned char ch = (unsigned char)E.ins_replay[i];
        if (ch >= 32 && ch < 127) {
            line_insert_char(&E.lines[E.cy], E.cx++, (char)ch);
            E.dirty = 1;
        }
    }
}

static void ins_exit(void)
{
    if (E.ins_current_len > 0) {
        int n = E.ins_current_len < (int)sizeof(E.ins_replay) - 1
                ? E.ins_current_len : (int)sizeof(E.ins_replay) - 1;
        memcpy(E.ins_replay, E.ins_current, n);
        E.ins_replay_len = n;
    }
    E.ins_current_len = 0;
    E.mode            = MODE_NORMAL;
    E.literal_next    = 0;
    E.ctrl_x_digits   = 0;
    clamp_cursor();
}

void process_insert(int c)
{
    if (E.ctrl_x_digits > 0) {
        int digit = -1;
        if (c >= '0' && c <= '9')      digit = c - '0';
        else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
        if (digit >= 0) {
            E.ctrl_x_hex    = E.ctrl_x_hex * 16 + digit;
            E.ctrl_x_digits--;
            if (E.ctrl_x_digits == 0 && E.ctrl_x_hex > 0) {
                line_insert_char(&E.lines[E.cy], E.cx++, (char)E.ctrl_x_hex);
                ins_record((char)E.ctrl_x_hex);
                E.dirty = 1;
            }
            return;
        }
        E.ctrl_x_digits = 0;
    }

    if (E.literal_next) {
        E.literal_next = 0;
        if (c >= 0 && c < 256) {
            line_insert_char(&E.lines[E.cy], E.cx++, (char)c);
            ins_record((char)c); E.dirty = 1;
        }
        return;
    }

    char seq[4];
    if (c >= KEY_UP) {
        seq[0] = '\x01';
        seq[1] = (char)((c >> 8) & 0xff);
        seq[2] = (char)(c & 0xff);
        seq[3] = '\0';
    } else {
        seq[0] = (char)c;
        seq[1] = '\0';
    }
    const char *mapped = map_lookup(seq, 1);
    if (mapped) { dispatch_rhs(mapped, 1); return; }

    switch (c) {
    case '\x1b': ins_exit(); break;
    case 0x03:   ins_exit(); break;
    case 0x00:   ins_replay_prev(); break;
    case 0x16:   E.literal_next = 1; break;
    case 0x04:   ins_ctrl_d(); break;
    case 0x14:   ins_ctrl_t(); break;
    case 0x17:   ins_ctrl_w(); break;
    case 0x15:   ins_erase_line(); break;
    case 0x18:   E.ctrl_x_hex = 0; E.ctrl_x_digits = 2; break;

    case KEY_LEFT:  E.cx--; clamp_cursor(); break;
    case KEY_RIGHT: E.cx++; clamp_cursor(); break;
    case KEY_UP:    E.cy--; clamp_cursor(); break;
    case KEY_DOWN:  E.cy++; clamp_cursor(); break;
    case KEY_HOME:  E.cx = 0; break;
    case KEY_END:   E.cx = E.lines[E.cy].len; break;
    case KEY_DEL:
        if (E.cx < E.lines[E.cy].len) {
            line_delete_char(&E.lines[E.cy], E.cx); E.dirty = 1;
        }
        break;

    case 127: case '\b':
        if (E.cx > 0) {
            line_delete_char(&E.lines[E.cy], E.cx - 1);
            E.cx--; E.dirty = 1;
        } else if (E.cy > 0) {
            int prev_len = E.lines[E.cy - 1].len;
            merge_lines(E.cy - 1, E.cy);
            E.cy--; E.cx = prev_len; E.dirty = 1;
        }
        break;

    case '\r': case '\n': {
        int wslen = 0;
        char wsbuf[256];

        if (E.opt_autoindent) {
            int prev_ws = leading_ws_len(E.cy);
            int copy_to = prev_ws < E.cx ? prev_ws : E.cx;
            Line *cur   = &E.lines[E.cy];
            for (int i = 0; i < copy_to && wslen < (int)sizeof(wsbuf) - 1; i++)
                wsbuf[wslen++] = (i < cur->len) ? cur->data[i] : ' ';
        }

        int content_from = E.cx;
        int rest         = E.lines[E.cy].len - content_from;
        if (rest < 0) rest = 0;

        insert_line_at(E.cy + 1);
        Line *newl = &E.lines[E.cy + 1];
        int   need = wslen + rest + 1;
        if (need > newl->cap) {
            newl->cap  = need + LINE_INIT;
            newl->data = realloc(newl->data, newl->cap);
        }
        int ni = 0;
        if (wslen > 0) { memcpy(newl->data, wsbuf, wslen); ni = wslen; }
        if (rest > 0) {
            memcpy(newl->data + ni, E.lines[E.cy].data + content_from, rest);
            ni += rest;
            E.lines[E.cy].len = content_from;
        }
        newl->len = ni;
        E.cy++;
        E.cx    = wslen;
        E.dirty = 1;
        ins_record('\n');
        break;
    }

    case '\t':
        if (E.opt_expandtab) {
            int  ts  = E.tabstop > 0 ? E.tabstop : 8;
            int  col = col_of_cx();
            int  n   = ts - (col % ts);
            if (n < 1) n = ts;
            for (int i = 0; i < n; i++) {
                line_insert_char(&E.lines[E.cy], E.cx++, ' ');
                ins_record(' ');
            }
            E.dirty = 1;
        } else {
            line_insert_char(&E.lines[E.cy], E.cx++, '\t');
            ins_record('\t');
            E.dirty = 1;
        }
        break;

    default:
        if (c >= 32 && c < 256) {
            if (E.mode == MODE_REPLACE && E.cx < E.lines[E.cy].len)
                E.lines[E.cy].data[E.cx++] = (char)c;
            else
                line_insert_char(&E.lines[E.cy], E.cx++, (char)c);
            ins_record((char)c); E.dirty = 1;
        }
        break;
    }
}

void process_ex_key(int c)
{
    switch (c) {
    case '\r': case '\n':
        E.ex_buf[E.ex_len] = '\0';
        dispatch_ex_cmd(E.ex_buf);
        E.mode = MODE_NORMAL; E.ex_len = 0; break;
    case '\x1b':
        E.mode = MODE_NORMAL; E.ex_len = 0; E.statusmsg[0] = '\0'; break;
    case 127: case '\b':
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
    case '\r': case '\n':
        do_search(E.search_dir); E.mode = MODE_NORMAL; break;
    case '\x1b':
        E.mode = MODE_NORMAL; E.search_len = 0; E.statusmsg[0] = '\0'; break;
    case 127: case '\b':
        if (E.search_len > 0) E.search_len--;
        else { E.mode = MODE_NORMAL; E.statusmsg[0] = '\0'; }
        break;
    default:
        if (c >= 32 && c < 127 && E.search_len < 254)
            E.search_buf[E.search_len++] = (char)c;
        break;
    }
}

void process_bang_key(int c)
{
    switch (c) {
    case '\r': case '\n': {
        if (E.bang_len == 0) {
            if (E.last_bang_cmd[0]) {
                strncpy(E.bang_buf, E.last_bang_cmd, sizeof(E.bang_buf) - 1);
                E.bang_buf[sizeof(E.bang_buf) - 1] = '\0';
                E.bang_len = (int)strlen(E.bang_buf);
            } else {
                snprintf(E.statusmsg, sizeof(E.statusmsg), "E34: No previous command");
                E.mode = MODE_NORMAL; break;
            }
        }
        E.bang_buf[E.bang_len] = '\0';
        char expanded[512];
        if (expand_cmd(E.bang_buf, expanded, sizeof(expanded)))
            shell_filter(E.bang_line1, E.bang_line2, expanded);
        E.mode = MODE_NORMAL; E.bang_len = 0; break;
    }
    case '\x1b':
        E.mode = MODE_NORMAL; E.bang_len = 0; E.statusmsg[0] = '\0'; break;
    case 127: case '\b':
        if (E.bang_len > 0) E.bang_len--;
        else { E.mode = MODE_NORMAL; E.statusmsg[0] = '\0'; }
        break;
    default:
        if (c >= 32 && c < 127 && E.bang_len < 254) {
            E.bang_buf[E.bang_len++] = (char)c;
            snprintf(E.statusmsg, sizeof(E.statusmsg),
                     "!%.*s", E.bang_len, E.bang_buf);
        }
        break;
    }
}
