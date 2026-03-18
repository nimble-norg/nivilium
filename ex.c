#include "vi.h"
#include <regex.h>

static void ex_out(const char *s)
{
    if (E.ex_mode) {
        if (!E.batch_mode) {
            write(STDOUT_FILENO, s, strlen(s));
            write(STDOUT_FILENO, "\n", 1);
        }
    } else {
        strncpy(E.statusmsg, s, sizeof(E.statusmsg) - 1);
        E.statusmsg[sizeof(E.statusmsg) - 1] = '\0';
    }
}

static void ex_outf(const char *fmt, ...)
{
    char    buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    ex_out(buf);
}

static void flush_status(void)
{
    if (E.ex_mode && !E.batch_mode && E.statusmsg[0]) {
        write(STDOUT_FILENO, E.statusmsg, strlen(E.statusmsg));
        write(STDOUT_FILENO, "\n", 1);
    }
    E.statusmsg[0] = '\0';
}

static void print_lines(int line1, int line2, int with_nums)
{
    if (!E.ex_mode) {
        E.cy = line2;
        clamp_cursor();
        return;
    }
    if (E.batch_mode) return;
    for (int i = line1; i <= line2 && i < E.nlines; i++) {
        if (with_nums) {
            char num[16];
            int  n = snprintf(num, sizeof(num), "%6d\t", i + 1);
            write(STDOUT_FILENO, num, n);
        }
        if (E.lines[i].len > 0)
            write(STDOUT_FILENO, E.lines[i].data, E.lines[i].len);
        write(STDOUT_FILENO, "\n", 1);
    }
}

static void list_lines(int line1, int line2)
{
    if (E.batch_mode) return;
    for (int i = line1; i <= line2 && i < E.nlines; i++) {
        Line *l = &E.lines[i];
        for (int j = 0; j < l->len; j++) {
            unsigned char c = (unsigned char)l->data[j];
            if (c == '\t') {
                write(STDOUT_FILENO, "^I", 2);
            } else if (c < 32) {
                char tmp[4];
                tmp[0] = '^';
                tmp[1] = (char)(c + '@');
                write(STDOUT_FILENO, tmp, 2);
            } else {
                write(STDOUT_FILENO, &l->data[j], 1);
            }
        }
        write(STDOUT_FILENO, "$\n", 2);
    }
}

static int parse_one_addr(const char *s, int *consumed)
{
    int i = 0;
    while (s[i] == ' ') i++;
    if (s[i] == '.') { *consumed = i + 1; return E.cy; }
    if (s[i] == '$') { *consumed = i + 1; return E.nlines > 0 ? E.nlines - 1 : 0; }
    if (isdigit((unsigned char)s[i])) {
        int n = 0;
        while (isdigit((unsigned char)s[i])) n = n * 10 + (s[i++] - '0');
        *consumed = i;
        return n > 0 ? n - 1 : 0;
    }
    *consumed = 0;
    return -1;
}

static void parse_range(const char *s, int *line1, int *line2,
                        int *consumed, int *has_range)
{
    *line1     = E.cy;
    *line2     = E.cy;
    *has_range = 0;
    int c      = 0;

    while (s[c] == ' ') c++;

    if (s[c] == '%') {
        *line1     = 0;
        *line2     = E.nlines > 0 ? E.nlines - 1 : 0;
        *consumed  = c + 1;
        *has_range = 1;
        return;
    }

    int c1;
    int a1 = parse_one_addr(s + c, &c1);
    if (c1 > 0) {
        *line1     = *line2 = a1;
        *has_range = 1;
        c         += c1;
        while (s[c] == ' ') c++;
        if (s[c] == ',') {
            c++;
            while (s[c] == ' ') c++;
            int c2;
            int a2 = parse_one_addr(s + c, &c2);
            if (c2 > 0) { *line2 = a2; c += c2; }
        }
    }
    *consumed = c;
}

static void do_substitute(int line1, int line2,
                          const char *pat, int pat_len,
                          const char *rep, int rep_len,
                          int global)
{
    char pat_str[256], rep_str[256];

    if (!pat || pat_len == 0) {
        if (E.last_sub_pat_len <= 0) {
            ex_out("E33: No previous substitute regular expression");
            return;
        }
        int n = E.last_sub_pat_len < 255 ? E.last_sub_pat_len : 255;
        memcpy(pat_str, E.last_sub_pat, n); pat_str[n] = '\0';
        n = E.last_sub_rep_len < 255 ? E.last_sub_rep_len : 255;
        memcpy(rep_str, E.last_sub_rep, n); rep_str[n] = '\0';
        global = E.last_sub_global;
    } else {
        int n = pat_len < 255 ? pat_len : 255;
        memcpy(pat_str, pat, n); pat_str[n] = '\0';
        memcpy(E.last_sub_pat, pat_str, n + 1);
        E.last_sub_pat_len = n;
        n = rep_len < 255 ? rep_len : 255;
        memcpy(rep_str, rep, n); rep_str[n] = '\0';
        memcpy(E.last_sub_rep, rep_str, n + 1);
        E.last_sub_rep_len = n;
        E.last_sub_global  = global;
    }

    regex_t re;
    if (regcomp(&re, pat_str, REG_EXTENDED) != 0) {
        ex_out("E383: Invalid search string");
        return;
    }

    int total_subs = 0, lines_changed = 0;
    save_undo();

    for (int i = line1; i <= line2 && i < E.nlines; i++) {
        Line  *l  = &E.lines[i];
        char  *lc = malloc(l->len + 1);
        memcpy(lc, l->data, l->len);
        lc[l->len] = '\0';

        char newbuf[8192];
        int  newlen = 0;
        const char *src = lc;
        int  did_sub = 0, first = 1;

        while (1) {
            regmatch_t m;
            int flags = first ? 0 : REG_NOTBOL;
            first = 0;
            if (regexec(&re, src, 1, &m, flags) != 0) {
                int rem = (int)strlen(src);
                if (newlen + rem < (int)sizeof(newbuf) - 1) {
                    memcpy(newbuf + newlen, src, rem);
                    newlen += rem;
                }
                break;
            }
            if (newlen + (int)m.rm_so < (int)sizeof(newbuf) - 1) {
                memcpy(newbuf + newlen, src, m.rm_so);
                newlen += (int)m.rm_so;
            }
            const char *rp = rep_str;
            while (*rp && newlen < (int)sizeof(newbuf) - 2) {
                if (*rp == '&') {
                    int ml = (int)(m.rm_eo - m.rm_so);
                    if (newlen + ml < (int)sizeof(newbuf) - 1) {
                        memcpy(newbuf + newlen, src + m.rm_so, ml);
                        newlen += ml;
                    }
                    rp++;
                } else if (*rp == '\\' && *(rp + 1)) {
                    rp++;
                    newbuf[newlen++] = *rp++;
                } else {
                    newbuf[newlen++] = *rp++;
                }
            }
            total_subs++; did_sub = 1;
            src += m.rm_eo;
            if (!global || m.rm_eo == m.rm_so) {
                int rem = (int)strlen(src);
                if (newlen + rem < (int)sizeof(newbuf) - 1) {
                    memcpy(newbuf + newlen, src, rem);
                    newlen += rem;
                }
                break;
            }
        }
        free(lc);

        if (did_sub) {
            if (newlen >= l->cap) {
                l->cap  = newlen + LINE_INIT;
                l->data = realloc(l->data, l->cap);
            }
            memcpy(l->data, newbuf, newlen);
            l->len  = newlen;
            E.dirty = 1;
            E.cy    = i;
            lines_changed++;
        }
    }
    regfree(&re);

    if (total_subs == 0)
        ex_outf("E486: Pattern not found: %s", pat_str);
    else
        ex_outf("%d substitution%s on %d line%s",
                total_subs,    total_subs    != 1 ? "s" : "",
                lines_changed, lines_changed != 1 ? "s" : "");
}

static void do_global(int line1, int line2,
                      const char *pat, const char *excmd, int invert)
{
    char pat_str[256];
    strncpy(pat_str, pat, sizeof(pat_str) - 1);
    pat_str[sizeof(pat_str) - 1] = '\0';

    regex_t re;
    if (regcomp(&re, pat_str, REG_EXTENDED) != 0) {
        ex_out("E383: Invalid search string");
        return;
    }

    int *mark = calloc(E.nlines, sizeof(int));
    for (int i = line1; i <= line2 && i < E.nlines; i++) {
        Line  *l  = &E.lines[i];
        char  *lc = malloc(l->len + 1);
        memcpy(lc, l->data, l->len);
        lc[l->len] = '\0';
        regmatch_t m;
        int does = (regexec(&re, lc, 1, &m, 0) == 0);
        mark[i]  = invert ? !does : does;
        free(lc);
    }
    regfree(&re);
    for (int i = 0; i < E.nlines; i++) {
        if (mark[i]) { E.cy = i; dispatch_ex_cmd(excmd); }
    }
    free(mark);
}

static void cmd_sub(int line1, int line2, const char *args)
{
    if (!args || !*args) {
        do_substitute(line1, line2, NULL, 0, NULL, 0, E.last_sub_global);
        return;
    }
    char delim = args[0];
    if (delim != '/' && delim != ',' && delim != '!' && delim != '@') {
        do_substitute(line1, line2, NULL, 0, NULL, 0, E.last_sub_global);
        return;
    }
    const char *p = args + 1;
    char pat[256]; int pat_len = 0;
    while (*p && *p != delim) {
        if (*p == '\\' && *(p + 1) == delim) {
            if (pat_len < 255) pat[pat_len++] = delim;
            p += 2;
        } else { if (pat_len < 255) pat[pat_len++] = *p++; }
    }
    if (*p == delim) p++;
    char rep[256]; int rep_len = 0;
    while (*p && *p != delim) {
        if (*p == '\\' && *(p + 1)) {
            if (rep_len < 254) { rep[rep_len++] = *p++; rep[rep_len++] = *p++; }
        } else { if (rep_len < 255) rep[rep_len++] = *p++; }
    }
    if (*p == delim) p++;
    int global = 0;
    while (*p) { if (*p == 'g') global = 1; p++; }
    do_substitute(line1, line2, pat, pat_len, rep, rep_len, global);
}

static void cmd_global(int line1, int line2, const char *args, int invert)
{
    if (!args || (args[0] != '/' && args[0] != '!' && args[0] != ',')) {
        ex_out("E488: Trailing characters");
        return;
    }
    char delim = args[0];
    const char *p = args + 1;
    char pat[256]; int plen = 0;
    while (*p && *p != delim) {
        if (*p == '\\' && *(p + 1) == delim) {
            if (plen < 255) pat[plen++] = delim;
            p += 2;
        } else { if (plen < 255) pat[plen++] = *p++; }
    }
    pat[plen] = '\0';
    if (*p == delim) p++;
    while (*p == ' ') p++;
    do_global(line1, line2, pat, p, invert);
}

static void cmd_delete(int line1, int line2)
{
    save_undo();
    int cnt = line2 - line1 + 1;
    for (int k = 0; k < cnt && line1 < E.nlines; k++) {
        free(E.lines[line1].data);
        memmove(&E.lines[line1], &E.lines[line1 + 1],
                sizeof(Line) * (E.nlines - line1 - 1));
        E.nlines--;
    }
    if (E.nlines == 0) insert_line_at(0);
    if (E.cy >= E.nlines) E.cy = E.nlines - 1;
    E.dirty = 1;
    clamp_cursor();
}

static void cmd_move(int line1, int line2, int dest)
{
    int n = line2 - line1 + 1;
    if (dest >= line1 && dest <= line2) {
        ex_out("E134: Move lines into themselves");
        return;
    }
    save_undo();
    char **saved = malloc(sizeof(char *) * n);
    int   *slens = malloc(sizeof(int) * n);
    for (int i = 0; i < n; i++) {
        slens[i] = E.lines[line1 + i].len;
        saved[i] = malloc(slens[i] + 1);
        memcpy(saved[i], E.lines[line1 + i].data, slens[i]);
    }
    cmd_delete(line1, line2);
    int ins = (dest > line2) ? dest - n : dest;
    for (int i = 0; i < n; i++) {
        insert_line_at(ins + 1 + i);
        Line *l = &E.lines[ins + 1 + i];
        if (slens[i] + 1 > l->cap) {
            l->cap  = slens[i] + LINE_INIT;
            l->data = realloc(l->data, l->cap);
        }
        memcpy(l->data, saved[i], slens[i]);
        l->len = slens[i];
        free(saved[i]);
    }
    free(saved); free(slens);
    E.cy = ins + n; E.dirty = 1; clamp_cursor();
}

static void cmd_copy(int line1, int line2, int dest)
{
    int n = line2 - line1 + 1;
    save_undo();
    for (int i = 0; i < n; i++) {
        insert_line_at(dest + 1 + i);
        Line *src = &E.lines[line1 + i];
        Line *dst = &E.lines[dest + 1 + i];
        if (src->len + 1 > dst->cap) {
            dst->cap  = src->len + LINE_INIT;
            dst->data = realloc(dst->data, dst->cap);
        }
        memcpy(dst->data, src->data, src->len);
        dst->len = src->len;
    }
    E.cy = dest + n; E.dirty = 1; clamp_cursor();
}

static void ex_input_lines(int after_line)
{
    char linebuf[512];
    int  ins = after_line;

    save_undo();
    if (!E.batch_mode)
        write(STDOUT_FILENO,
              "\t(enter text; press Ctrl-C to finish)\n", 38);

    while (1) {
        if (!E.batch_mode)
            write(STDOUT_FILENO, "\t", 1);

        if (!fgets(linebuf, sizeof(linebuf), stdin)) {
            E.sig_int = 0;
            break;
        }

        int l = (int)strlen(linebuf);
        if (l > 0 && linebuf[l - 1] == '\n') linebuf[--l] = '\0';

        insert_line_at(ins + 1);
        ins++;
        Line *cur = &E.lines[ins];
        if (l + 1 > cur->cap) {
            cur->cap  = l + LINE_INIT;
            cur->data = realloc(cur->data, cur->cap);
        }
        if (l > 0) memcpy(cur->data, linebuf, l);
        cur->len = l;
        E.dirty  = 1;
    }

    E.cy = ins > 0 ? ins : 0;
    clamp_cursor();
}

static void cmd_show_options(void)
{
    char buf[512];
    snprintf(buf, sizeof(buf),
             "autoindent=%s  syntax=%s  number=%s  ruler=%s  showmode=%s\n"
             "tabstop=%d  shiftwidth=%d  readonly=%s",
             E.opt_autoindent ? "on" : "off",
             E.opt_syntax     ? "on" : "off",
             E.opt_number     ? "on" : "off",
             E.opt_ruler      ? "on" : "off",
             E.opt_showmode   ? "on" : "off",
             E.tabstop, E.shiftwidth,
             E.readonly       ? "on" : "off");
    ex_out(buf);
}

void dispatch_ex_cmd(const char *raw)
{
    if (!raw) return;
    const char *s = raw;
    while (*s == ' ') s++;
    if (!*s) return;
    if (*s == '"') return;

    int line1, line2, consumed, has_range;
    parse_range(s, &line1, &line2, &consumed, &has_range);
    s += consumed;
    while (*s == ' ') s++;

    if (line1 < 0)         line1 = 0;
    if (line2 < 0)         line2 = 0;
    if (line1 >= E.nlines) line1 = E.nlines > 0 ? E.nlines - 1 : 0;
    if (line2 >= E.nlines) line2 = E.nlines > 0 ? E.nlines - 1 : 0;
    if (line2 < line1)     line2 = line1;

    if (*s == '\0') {
        E.cy = line2;
        clamp_cursor();
        if (E.ex_mode && !E.batch_mode) {
            if (E.lines[E.cy].len > 0)
                write(STDOUT_FILENO, E.lines[E.cy].data, E.lines[E.cy].len);
            write(STDOUT_FILENO, "\n", 1);
        }
        return;
    }

    if (s[0] == '!' && s[1] == '\0' && !has_range) {
        ex_out("E471: Argument required");
        return;
    }

    if (s[0] == '!') {
        const char *arg = s + 1;
        while (*arg == ' ') arg++;
        char expanded[512];
        if (!expand_cmd(arg, expanded, sizeof(expanded))) { flush_status(); return; }
        if (expanded[0] == '\0') {
            if (E.last_bang_cmd[0]) {
                strncpy(expanded, E.last_bang_cmd, sizeof(expanded) - 1);
            } else { ex_out("E34: No previous command"); return; }
        }
        if (has_range) {
            shell_filter(line1, line2, expanded); flush_status();
        } else {
            shell_exec(expanded); flush_status();
        }
        return;
    }

    if (s[0] == 'q' && s[1] == '!') { exit(0); }

    if ((s[0] == 'q' && (s[1] == '\0' || s[1] == ' '))
        || strncmp(s, "quit!", 5) == 0) {
        if (strncmp(s, "quit!", 5) == 0) { exit(0); }
        if (E.dirty)
            ex_out("E37: No write since last change (use :q! to override)");
        else
            exit(0);
        return;
    }

    if (strcmp(s, "vi")  == 0    || strcmp(s, "visual") == 0
        || strncmp(s, "vi ",  3) == 0 || strncmp(s, "visual ", 7) == 0) {
        E.ex_mode = 0;
        return;
    }

    if (strcmp(s, "wq") == 0 || strcmp(s, "x") == 0
        || strcmp(s, "xit") == 0 || strcmp(s, "exit") == 0) {
        if (E.filename[0]) {
            if (E.dirty) save_file(E.filename);
            flush_status();
            if (!E.dirty) exit(0);
        } else if (!E.dirty) { exit(0); }
        else { ex_out("E32: No file name"); }
        return;
    }

    if (strncmp(s, "wq ", 3) == 0) {
        const char *arg = s + 3;
        while (*arg == ' ') arg++;
        if (*arg) {
            strncpy(E.filename, arg, sizeof(E.filename) - 1);
            E.filename[sizeof(E.filename) - 1] = '\0';
        }
        if (E.filename[0]) {
            save_file(E.filename); flush_status();
            if (!E.dirty) exit(0);
        } else { ex_out("E32: No file name"); }
        return;
    }

    if ((s[0] == 'w' && (s[1] == '\0' || s[1] == ' '
                         || s[1] == '!' || s[1] == '>'))
        || strncmp(s, "write", 5) == 0) {
        const char *arg = (strncmp(s, "write", 5) == 0) ? s + 5 : s + 1;
        if (*arg == '!') arg++;
        while (*arg == ' ') arg++;
        if (*arg) {
            strncpy(E.filename, arg, sizeof(E.filename) - 1);
            E.filename[sizeof(E.filename) - 1] = '\0';
        }
        if (E.filename[0]) { save_file(E.filename); flush_status(); }
        else ex_out("E32: No file name");
        return;
    }

    if ((s[0] == 'e' && (s[1] == '\0' || s[1] == ' ' || s[1] == '!'))
        || strncmp(s, "edit", 4) == 0) {
        const char *arg = (strncmp(s, "edit", 4) == 0) ? s + 4 : s + 1;
        int force = 0;
        if (*arg == '!') { force = 1; arg++; }
        while (*arg == ' ') arg++;
        if (E.dirty && !force) {
            ex_out("E37: No write since last change (use :e! to override)");
            return;
        }
        for (int i = 0; i < E.nlines; i++) free(E.lines[i].data);
        E.nlines = 0; E.cx = 0; E.cy = 0; E.rowoff = 0; E.coloff = 0; E.dirty = 0;
        if (*arg) {
            strncpy(E.filename, arg, sizeof(E.filename) - 1);
            E.filename[sizeof(E.filename) - 1] = '\0';
        }
        if (E.filename[0]) {
            detect_filetype();
            load_file(E.filename);
            source_exrc("/etc/vi.exrc");
            const char *home = getenv("HOME");
            if (home) {
                char path[512];
                snprintf(path, sizeof(path), "%s/.exrc", home);
                source_exrc(path);
            }
            flush_status();
        } else {
            insert_line_at(0);
        }
        return;
    }

    if ((s[0] == 'r' && s[1] == '!') || strncmp(s, "read!", 5) == 0) {
        const char *arg = (strncmp(s, "read!", 5) == 0) ? s + 5 : s + 2;
        while (*arg == ' ') arg++;
        if (*arg) {
            char expanded[512];
            if (!expand_cmd(arg, expanded, sizeof(expanded))) {
                flush_status(); return;
            }
            shell_read(line2, expanded); flush_status();
        }
        return;
    }

    if ((s[0] == 'r' && (s[1] == '\0' || s[1] == ' '))
        || strncmp(s, "read", 4) == 0) {
        const char *arg = (strncmp(s, "read", 4) == 0) ? s + 4 : s + 1;
        while (*arg == ' ') arg++;
        if (*arg) {
            char expanded[512];
            if (!expand_cmd(arg, expanded, sizeof(expanded))) {
                flush_status(); return;
            }
            FILE *f = fopen(expanded, "r");
            if (!f) { ex_outf("E484: Can't open file %s", expanded); return; }
            save_undo();
            char buf[4096];
            int  ins = line2, nlines = 0;
            while (fgets(buf, sizeof(buf), f)) {
                int l = (int)strlen(buf);
                if (l > 0 && buf[l - 1] == '\n') buf[--l] = '\0';
                insert_line_at(ins + 1); ins++;
                Line *cur = &E.lines[ins];
                if (l + 1 > cur->cap) {
                    cur->cap  = l + LINE_INIT;
                    cur->data = realloc(cur->data, cur->cap);
                }
                memcpy(cur->data, buf, l); cur->len = l; nlines++;
            }
            fclose(f);
            E.cy = ins; E.dirty = 1; clamp_cursor();
            ex_outf("%s: %d line%s", expanded, nlines, nlines != 1 ? "s" : "");
        }
        return;
    }

    if ((s[0] == 'a' && (s[1] == '\0' || s[1] == '!'))
        || strncmp(s, "append", 6) == 0) {
        if (E.ex_mode)
            ex_input_lines(line2);
        return;
    }

    if ((s[0] == 'i' && (s[1] == '\0' || s[1] == '!'))
        || strncmp(s, "insert", 6) == 0) {
        if (E.ex_mode)
            ex_input_lines(line1 > 0 ? line1 - 1 : 0);
        else {
            save_undo();
            insert_line_at(line1 > 0 ? line1 - 1 : 0);
            E.cy  = line1 > 0 ? line1 - 1 : 0;
            E.cx  = 0;
            E.mode = MODE_INSERT;
            E.ins_current_len = 0;
        }
        return;
    }

    if ((s[0] == 'c' && (s[1] == '\0' || s[1] == '!'))
        || strncmp(s, "change", 6) == 0) {
        cmd_delete(line1, line2);
        if (E.ex_mode) {
            ex_input_lines(line1 > 0 ? line1 - 1 : 0);
        } else {
            E.cy  = line1 > 0 ? line1 - 1 : 0;
            E.cx  = 0;
            E.mode = MODE_INSERT;
            E.ins_current_len = 0;
        }
        return;
    }

    if ((s[0] == 'd' && (s[1] == '\0' || s[1] == ' '))
        || strncmp(s, "delete", 6) == 0) {
        cmd_delete(line1, line2); return;
    }

    if ((strncmp(s, "co", 2) == 0 && (s[2] == 'p' || s[2] == ' ' || s[2] == '\0'))
        || s[0] == 't') {
        const char *arg = (s[0] == 't') ? s + 1 : s + 2;
        if (s[0] != 't' && s[2] == 'p') arg = s + 4;
        while (*arg == ' ') arg++;
        int c2, dest;
        dest = parse_one_addr(arg, &c2);
        if (c2 > 0) cmd_copy(line1, line2, dest);
        else         ex_out("E14: Invalid address");
        return;
    }

    if ((strncmp(s, "mo", 2) == 0 && (s[2] == 'v' || s[2] == 'e' || s[2] == ' '))
        || strncmp(s, "move", 4) == 0) {
        const char *arg = (strncmp(s, "move", 4) == 0) ? s + 4 : s + 2;
        while (*arg == ' ') arg++;
        int c2, dest;
        dest = parse_one_addr(arg, &c2);
        if (c2 > 0) cmd_move(line1, line2, dest);
        else         ex_out("E14: Invalid address");
        return;
    }

    if (s[0] == 'l' || strncmp(s, "list", 4) == 0) {
        list_lines(line1, line2); return;
    }

    if (s[0] == 'p' || strncmp(s, "print", 5) == 0) {
        print_lines(line1, line2, 0); return;
    }

    if (strncmp(s, "nu", 2) == 0 || strncmp(s, "number", 6) == 0 || s[0] == '#') {
        print_lines(line1, line2, 1); return;
    }

    if (s[0] == '=') {
        ex_outf("%d", line2 + 1); return;
    }

    if ((s[0] == 'f' && (s[1] == '\0' || s[1] == ' '))
        || strncmp(s, "file", 4) == 0) {
        const char *arg = (strncmp(s, "file", 4) == 0) ? s + 4 : s + 1;
        while (*arg == ' ') arg++;
        if (*arg) {
            strncpy(E.filename, arg, sizeof(E.filename) - 1);
            E.filename[sizeof(E.filename) - 1] = '\0';
            detect_filetype();
        }
        const char *fn = E.filename[0] ? E.filename : "[No Name]";
        ex_outf("\"%s\"%s%s  line %d of %d  --%d%%--",
                fn,
                E.readonly ? " [readonly]" : "",
                E.dirty    ? " [modified]" : "",
                E.cy + 1, E.nlines,
                E.nlines > 0 ? (int)((long)(E.cy + 1) * 100 / E.nlines) : 0);
        return;
    }

    if ((s[0] == 'u' && (s[1] == '\0' || s[1] == ' '))
        || strncmp(s, "undo", 4) == 0) {
        do_undo(); flush_status(); return;
    }

    if ((s[0] == 'y' && (s[1] == '\0' || s[1] == 'a' || s[1] == ' '))
        || strncmp(s, "yank", 4) == 0) {
        free(E.yank_data);
        int total = 0;
        for (int i = line1; i <= line2 && i < E.nlines; i++)
            total += E.lines[i].len + 1;
        E.yank_data = malloc(total + 1);
        int pos = 0;
        for (int i = line1; i <= line2 && i < E.nlines; i++) {
            memcpy(E.yank_data + pos, E.lines[i].data, E.lines[i].len);
            pos += E.lines[i].len;
            if (i < line2) E.yank_data[pos++] = '\n';
        }
        E.yank_len = pos;
        ex_outf("%d line%s yanked", line2 - line1 + 1,
                line2 - line1 > 0 ? "s" : "");
        return;
    }

    if ((s[0] == 'p' && s[1] == 'u') || strncmp(s, "put", 3) == 0) {
        if (E.yank_data) {
            save_undo();
            int ins = line2;
            insert_line_at(ins + 1); ins++;
            Line *l = &E.lines[ins];
            if (E.yank_len + 1 > l->cap) {
                l->cap  = E.yank_len + LINE_INIT;
                l->data = realloc(l->data, l->cap);
            }
            memcpy(l->data, E.yank_data, E.yank_len);
            l->len = E.yank_len; E.cy = ins; E.dirty = 1; clamp_cursor();
        }
        return;
    }

    if (s[0] == 'j' || strncmp(s, "join", 4) == 0) {
        save_undo();
        int n = line2 - line1;
        for (int k = 0; k < n && line1 + 1 < E.nlines; k++) {
            Line *u = &E.lines[line1];
            if (u->len > 0) line_insert_char(u, u->len, ' ');
            merge_lines(line1, line1 + 1);
        }
        E.cy = line1; E.dirty = 1; clamp_cursor();
        return;
    }

    if ((s[0] == 'g' && (s[1] == '/' || s[1] == '!' || s[1] == ','))
        || strncmp(s, "global", 6) == 0) {
        const char *arg = (strncmp(s, "global", 6) == 0) ? s + 6 : s + 1;
        while (*arg == ' ') arg++;
        cmd_global(line1, line2, arg, 0);
        return;
    }

    if ((s[0] == 'v' && (s[1] == '/' || s[1] == '!' || s[1] == ','))
        || strncmp(s, "vglobal", 7) == 0) {
        const char *arg = (strncmp(s, "vglobal", 7) == 0) ? s + 7 : s + 1;
        while (*arg == ' ') arg++;
        cmd_global(line1, line2, arg, 1);
        return;
    }

    if ((s[0] == 's' && (s[1] == '\0' || s[1] == '/' || s[1] == ','
                         || s[1] == '!' || s[1] == '@' || s[1] == ' '))
        || strncmp(s, "substitute", 10) == 0) {
        const char *arg = (strncmp(s, "substitute", 10) == 0) ? s + 10 : s + 1;
        while (*arg == ' ') arg++;
        cmd_sub(line1, line2, arg);
        return;
    }

    if (s[0] == '&') {
        do_substitute(line1, line2, NULL, 0, NULL, 0, E.last_sub_global);
        return;
    }

    if (strncmp(s, "set", 3) == 0 && (s[3] == '\0' || s[3] == ' ')) {
        const char *arg = s + 3;
        while (*arg == ' ') arg++;
        if (!*arg || strcmp(arg, "all") == 0) {
            cmd_show_options();
        } else {
            run_ex_line(s);
        }
        return;
    }

    if (strncmp(s, "syntax", 6) == 0 && (s[6] == ' ' || s[6] == '\0')) {
        const char *arg = s + 6;
        while (*arg == ' ') arg++;
        if (strcmp(arg, "on") == 0)      E.opt_syntax = 1;
        else if (strcmp(arg, "off") == 0) E.opt_syntax = 0;
        return;
    }

    if ((strncmp(s, "so", 2) == 0 && (s[2] == ' ' || s[2] == '\0'))
        || strncmp(s, "source", 6) == 0) {
        const char *arg = (strncmp(s, "source", 6) == 0) ? s + 6 : s + 2;
        while (*arg == ' ') arg++;
        if (*arg) source_exrc(arg);
        return;
    }

    if (strncmp(s, "noremap!", 8) == 0) {
        run_ex_line(s); return;
    }
    if (strncmp(s, "noremap", 7) == 0 && (s[7] == ' ' || s[7] == '\0')) {
        run_ex_line(s); return;
    }
    if (strncmp(s, "map!", 4) == 0) {
        run_ex_line(s); return;
    }
    if (strncmp(s, "map", 3) == 0 && (s[3] == ' ' || s[3] == '\0')) {
        run_ex_line(s); return;
    }
    if (strncmp(s, "unmap!", 6) == 0) {
        run_ex_line(s); return;
    }
    if (strncmp(s, "unmap", 5) == 0 && (s[5] == ' ' || s[5] == '\0')) {
        run_ex_line(s); return;
    }

    if (strncmp(s, "color", 5) == 0 && (s[5] == ' ' || s[5] == '\0')) {
        run_ex_line(s); return;
    }
    if (strncmp(s, "colour", 6) == 0 && (s[6] == ' ' || s[6] == '\0')) {
        run_ex_line(s); return;
    }
    if (strncmp(s, "syntax", 6) == 0 && (s[6] == ' ' || s[6] == '\0')) {
        const char *arg = s + 6;
        while (*arg == ' ') arg++;
        if (strcmp(arg, "on") == 0)       E.opt_syntax = 1;
        else if (strcmp(arg, "off") == 0)  E.opt_syntax = 0;
        return;
    }

    if (strncmp(s, "version", 7) == 0 || strcmp(s, "ve") == 0) {
        ex_out("vi clone 1.0 (C99/POSIX.1-2008)");
        return;
    }

    ex_outf("E492: Not an editor command: %s", s);
}

void run_ex_mode(void)
{
    char linebuf[512];

    if (!E.batch_mode)
        write(STDOUT_FILENO,
              "Ex mode.  Type 'visual' to go to visual mode.\n", 47);

    while (E.ex_mode) {
        if (!E.batch_mode)
            write(STDOUT_FILENO, ":", 1);

        if (!fgets(linebuf, sizeof(linebuf), stdin)) {
            E.sig_int = 0;
            if (E.dirty && !E.batch_mode)
                write(STDERR_FILENO, "No write since last change\n", 27);
            exit(0);
        }

        int l = (int)strlen(linebuf);
        if (l > 0 && linebuf[l - 1] == '\n') linebuf[--l] = '\0';

        if (l == 0) {
            if (!E.batch_mode) {
                if (E.cy + 1 < E.nlines) E.cy++;
                if (E.lines[E.cy].len > 0)
                    write(STDOUT_FILENO, E.lines[E.cy].data, E.lines[E.cy].len);
                write(STDOUT_FILENO, "\n", 1);
            }
            continue;
        }

        dispatch_ex_cmd(linebuf);
    }
}
