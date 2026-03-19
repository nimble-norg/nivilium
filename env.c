#include "vi.h"

void map_parse_lhs(const char *src, char *dst, int dstsz);

const char *color_get(const char *name)
{
    for (int i = 0; i < E.ncolors; i++) {
        if (!strcmp(E.colors[i].name, name))
            return E.colors[i].value;
    }
    return NULL;
}

void color_set(const char *name, const char *value)
{
    for (int i = 0; i < E.ncolors; i++) {
        if (!strcmp(E.colors[i].name, name)) {
            strncpy(E.colors[i].value, value, sizeof(E.colors[i].value) - 1);
            E.colors[i].value[sizeof(E.colors[i].value) - 1] = '\0';
            return;
        }
    }
    if (E.ncolors < 64) {
        snprintf(E.colors[E.ncolors].name,
                 sizeof(E.colors[E.ncolors].name),  "%s", name);
        snprintf(E.colors[E.ncolors].value,
                 sizeof(E.colors[E.ncolors].value), "%s", value);
        E.ncolors++;
    }
}

static void init_vim_colors(void)
{
    color_set("normal",    "\x1b[0m");
    color_set("comment",   "\x1b[2;37m");
    color_set("keyword",   "\x1b[1;34m");
    color_set("type",      "\x1b[0;36m");
    color_set("string",    "\x1b[0;32m");
    color_set("number",    "\x1b[0;33m");
    color_set("preproc",   "\x1b[0;35m");
    color_set("linenr",    "\x1b[2;37m");
    color_set("linenr_cur","\x1b[1;33m");
    color_set("statusbar", "\x1b[7m");
    color_set("tilde",     "\x1b[2;34m");
    color_set("search",    "\x1b[0;43;30m");
    color_set("error",     "\x1b[41;1;37m");
}

static void parse_color_cmd(const char *s)
{
    while (*s == ' ') s++;
    char name[32], val[32];
    int ni = 0, vi = 0;
    while (*s && *s != ' ' && ni < 31) name[ni++] = *s++;
    name[ni] = '\0';
    while (*s == ' ') s++;
    while (*s && vi < 31) val[vi++] = *s++;
    val[vi] = '\0';
    if (!ni || !vi) return;

    if (val[0] == '#' && strlen(val) == 7) {
        int r = 0, g = 0, b = 0;
        sscanf(val + 1, "%02x%02x%02x", &r, &g, &b);
        char esc[32];
        snprintf(esc, sizeof(esc), "\x1b[38;2;%d;%d;%dm", r, g, b);
        color_set(name, esc);
    } else if (strncmp(val, "\\e[", 3) == 0 || val[0] == '\x1b') {
        color_set(name, val);
    } else {
        int bold = 0, col = -1;
        if (!strcmp(val, "bold"))      { bold = 1; }
        else if (!strcmp(val, "red"))    col = 31;
        else if (!strcmp(val, "green"))  col = 32;
        else if (!strcmp(val, "yellow")) col = 33;
        else if (!strcmp(val, "blue"))   col = 34;
        else if (!strcmp(val, "magenta"))col = 35;
        else if (!strcmp(val, "cyan"))   col = 36;
        else if (!strcmp(val, "white"))  col = 37;
        else if (!strcmp(val, "gray") || !strcmp(val, "grey"))
            col = 90;
        char esc[32];
        if (bold && col >= 0)
            snprintf(esc, sizeof(esc), "\x1b[1;%dm", col);
        else if (bold)
            snprintf(esc, sizeof(esc), "\x1b[1m");
        else if (col >= 0)
            snprintf(esc, sizeof(esc), "\x1b[%dm", col);
        else
            snprintf(esc, sizeof(esc), "\x1b[0m");
        color_set(name, esc);
    }
}

static void parse_map_cmd(const char *s, int noremap, int for_insert)
{
    while (*s == ' ') s++;
    if (!*s) { map_show(for_insert); return; }

    char lhs[64];
    map_parse_lhs(s, lhs, sizeof(lhs));

    const char *p = s;
    while (*p && *p != ' ') p++;
    while (*p == ' ') p++;

    if (!*p) {
        const char *rhs = map_lookup(lhs, for_insert);
        if (rhs) {
            char buf[512];
            snprintf(buf, sizeof(buf), "%s -> %s", lhs, rhs);
            if (E.ex_mode) {
                write(STDOUT_FILENO, buf, strlen(buf));
                write(STDOUT_FILENO, "\n", 1);
            } else {
                snprintf(E.statusmsg, sizeof(E.statusmsg), "%s", buf);
            }
        }
        return;
    }

    char rhs[256];
    map_parse_lhs(p, rhs, sizeof(rhs));
    map_add(lhs, rhs, noremap, for_insert);
}

static void handle_set_token(const char *tok)
{
    if (!*tok) return;

    if (strncmp(tok, "no", 2) == 0) {
        const char *name = tok + 2;
        if (!strcmp(name, "autoindent") || !strcmp(name, "ai"))
            E.opt_autoindent = 0;
        else if (!strcmp(name, "syntax")  || !strcmp(name, "syn"))
            E.opt_syntax = 0;
        else if (!strcmp(name, "number")  || !strcmp(name, "nu"))
            E.opt_number = 0;
        else if (!strcmp(name, "ruler")   || !strcmp(name, "ru"))
            E.opt_ruler = 0;
        else if (!strcmp(name, "showmode")|| !strcmp(name, "smd"))
            E.opt_showmode = 0;
        else if (!strcmp(name, "readonly")|| !strcmp(name, "ro"))
            E.readonly = 0;
        else if (!strcmp(name, "colors"))
            E.opt_colors = 0;
        else if (!strcmp(name, "expandtab") || !strcmp(name, "et"))
            E.opt_expandtab = 0;
        return;
    }

    const char *eq = strchr(tok, '=');
    if (eq) {
        char name[64];
        int  nlen = (int)(eq - tok);
        if (nlen >= (int)sizeof(name)) nlen = (int)sizeof(name) - 1;
        memcpy(name, tok, nlen);
        name[nlen] = '\0';
        const char *val = eq + 1;

        if (!strcmp(name, "tabstop") || !strcmp(name, "ts")) {
            int v = atoi(val);
            if (v > 0) E.tabstop = v;
        } else if (!strcmp(name, "shiftwidth") || !strcmp(name, "sw")) {
            int v = atoi(val);
            if (v > 0) E.shiftwidth = v;
        } else if (!strcmp(name, "statusfmt") || !strcmp(name, "sf")) {
            strncpy(E.opt_statusfmt, val, sizeof(E.opt_statusfmt) - 1);
            E.opt_statusfmt[sizeof(E.opt_statusfmt) - 1] = '\0';
        } else if (!strcmp(name, "filetype") || !strcmp(name, "ft")) {
            strncpy(E.filetype, val, sizeof(E.filetype) - 1);
            E.filetype[sizeof(E.filetype) - 1] = '\0';
        } else if (!strcmp(name, "indentchar") || !strcmp(name, "ic")) {
            if (val[0] == 't' && val[1] == '\0')
                E.opt_indentchar = '\t';
            else if (val[0] == 's' && val[1] == '\0')
                E.opt_indentchar = ' ';
            else if (val[0])
                E.opt_indentchar = val[0];
        }
        return;
    }

    if (!strcmp(tok, "autoindent") || !strcmp(tok, "ai"))
        E.opt_autoindent = 1;
    else if (!strcmp(tok, "syntax")   || !strcmp(tok, "syn"))
        E.opt_syntax = 1;
    else if (!strcmp(tok, "number")   || !strcmp(tok, "nu"))
        E.opt_number = 1;
    else if (!strcmp(tok, "ruler")    || !strcmp(tok, "ru"))
        E.opt_ruler = 1;
    else if (!strcmp(tok, "showmode") || !strcmp(tok, "smd"))
        E.opt_showmode = 1;
    else if (!strcmp(tok, "readonly") || !strcmp(tok, "ro"))
        E.readonly = 1;
    else if (!strcmp(tok, "colors"))
        E.opt_colors = 1;
    else if (!strcmp(tok, "expandtab") || !strcmp(tok, "et"))
        E.opt_expandtab = 1;
}

void run_ex_line(const char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    if (*s == ':') s++;
    while (*s == ' ' || *s == '\t') s++;

    if (strncmp(s, "set", 3) == 0 && (s[3] == ' ' || s[3] == '\0')) {
        const char *p = s + 3;
        while (*p == ' ') p++;
        if (!*p) return;
        char buf[512];
        strncpy(buf, p, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        char *tok = buf;
        while (*tok) {
            while (*tok == ' ') tok++;
            if (!*tok) break;
            char *end = tok;
            while (*end && *end != ' ') end++;
            char save = *end;
            *end = '\0';
            handle_set_token(tok);
            *end = save;
            tok  = end;
        }
    } else if (strncmp(s, "syntax on", 9) == 0) {
        E.opt_syntax = 1;
    } else if (strncmp(s, "syntax off", 10) == 0) {
        E.opt_syntax = 0;
    } else if (strncmp(s, "color ", 6) == 0) {
        parse_color_cmd(s + 6);
    } else if (strncmp(s, "colour ", 7) == 0) {
        parse_color_cmd(s + 7);
    } else if (strncmp(s, "noremap!", 8) == 0) {
        parse_map_cmd(s + 8, 1, 1);
    } else if (strncmp(s, "noremap", 7) == 0 && (s[7] == ' ' || s[7] == '\0')) {
        parse_map_cmd(s + 7, 1, 0);
    } else if (strncmp(s, "map!", 4) == 0) {
        parse_map_cmd(s + 4, 0, 1);
    } else if (strncmp(s, "map", 3) == 0 && (s[3] == ' ' || s[3] == '\0')) {
        parse_map_cmd(s + 3, 0, 0);
    } else if (strncmp(s, "unmap!", 6) == 0) {
        char lhs[64];
        map_parse_lhs(s + 6, lhs, sizeof(lhs));
        map_remove(lhs, 1);
    } else if (strncmp(s, "unmap", 5) == 0 && (s[5] == ' ' || s[5] == '\0')) {
        char lhs[64];
        map_parse_lhs(s + 5, lhs, sizeof(lhs));
        map_remove(lhs, 0);
    }
}

void source_exrc(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        int l = (int)strlen(line);
        if (l > 0 && line[l - 1] == '\n') line[--l] = '\0';
        if (l > 0) run_ex_line(line);
    }
    fclose(f);
}

static void parse_exinit(const char *src)
{
    char buf[1024];
    strncpy(buf, src, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    char *p = buf, *end;
    while ((end = strchr(p, '|')) != NULL) {
        *end = '\0';
        run_ex_line(p);
        p = end + 1;
    }
    if (*p) run_ex_line(p);
}

void detect_filetype(void)
{
    E.filetype[0] = '\0';
    if (!E.filename[0]) return;

    const char *dot = strrchr(E.filename, '.');
    if (!dot) {
        strncpy(E.filetype, "sh", sizeof(E.filetype) - 1);
        return;
    }
    dot++;
    if (!strcmp(dot, "c") || !strcmp(dot, "h"))
        strncpy(E.filetype, "c", sizeof(E.filetype) - 1);
    else if (!strcmp(dot, "cpp") || !strcmp(dot, "cc") ||
             !strcmp(dot, "cxx") || !strcmp(dot, "hpp"))
        strncpy(E.filetype, "cpp", sizeof(E.filetype) - 1);
    else if (!strcmp(dot, "sh") || !strcmp(dot, "bash"))
        strncpy(E.filetype, "sh", sizeof(E.filetype) - 1);
    else if (!strcmp(dot, "py"))
        strncpy(E.filetype, "py", sizeof(E.filetype) - 1);
    else if (!strcmp(dot, "mk") || !strcmp(dot, "mak"))
        strncpy(E.filetype, "make", sizeof(E.filetype) - 1);
    else
        strncpy(E.filetype, dot, sizeof(E.filetype) - 1);
    E.filetype[sizeof(E.filetype) - 1] = '\0';
}

static void load_rc_files(void)
{
    source_exrc("/etc/vi.exrc");

    const char *nxi  = getenv("NEXINIT");
    const char *xi   = getenv("EXINIT");
    const char *home = getenv("HOME");

    if (!nxi && !xi && home) {
        char path[512];
        snprintf(path, sizeof(path), "%s/.nexrc", home);
        FILE *f = fopen(path, "r");
        if (f) {
            fclose(f);
            source_exrc(path);
        } else {
            snprintf(path, sizeof(path), "%s/.exrc", home);
            source_exrc(path);
        }
    }

    if (nxi) parse_exinit(nxi);
    if (xi && !nxi) parse_exinit(xi);
}

void read_env(void)
{
    E.shiftwidth      = 4;
    E.tabstop         = 8;
    E.pending_buf     = -1;
    E.search_dir      = 1;
    E.opt_ruler       = 1;
    E.opt_showmode    = 1;
    E.opt_indentchar  = '\t';
    E.opt_statusfmt[0] = '\0';

    const char *t = getenv("TERM");
    if (t) strncpy(E.term_name, t, sizeof(E.term_name) - 1);

    const char *sh = getenv("SHELL");
    strncpy(E.shell, sh ? sh : "/bin/sh", sizeof(E.shell) - 1);

    const char *cols_s = getenv("COLUMNS");
    if (cols_s) { int c = atoi(cols_s); if (c > 10) E.cols = c; }

    const char *lines_s = getenv("LINES");
    if (lines_s) { int r = atoi(lines_s); if (r > 2) E.rows = r - 2; }

    init_vim_colors();
    load_rc_files();
}
