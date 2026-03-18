#include "vi.h"

void run_ex_line(const char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    if (*s == ':') s++;
    while (*s == ' ' || *s == '\t') s++;

    if (strncmp(s, "set", 3) == 0 && (s[3] == ' ' || s[3] == '\0')) {
        const char *opt = s + 3;
        while (*opt == ' ') opt++;
        if (strcmp(opt, "readonly") == 0 || strcmp(opt, "ro") == 0)
            E.readonly = 1;
        else if (strcmp(opt, "noreadonly") == 0 || strcmp(opt, "noro") == 0)
            E.readonly = 0;
    }
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

static void source_exrc(const char *path)
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

void read_env(void)
{
    E.shiftwidth  = 8;
    E.tabstop     = 8;
    E.pending_buf = -1;
    E.search_dir  = 1;

    const char *t = getenv("TERM");
    if (t) strncpy(E.term_name, t, sizeof(E.term_name) - 1);

    const char *sh = getenv("SHELL");
    strncpy(E.shell, sh ? sh : "/bin/sh", sizeof(E.shell) - 1);

    const char *cols_s = getenv("COLUMNS");
    if (cols_s) {
        int c = atoi(cols_s);
        if (c > 10) E.cols = c;
    }

    const char *lines_s = getenv("LINES");
    if (lines_s) {
        int r = atoi(lines_s);
        if (r > 2) E.rows = r - 2;
    }

    source_exrc("/etc/vi.exrc");

    const char *nxi = getenv("NEXINIT");
    const char *xi  = getenv("EXINIT");

    if (!nxi && !xi) {
        const char *home = getenv("HOME");
        if (home) {
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
    }

    if (nxi) parse_exinit(nxi);
    if (xi && !nxi) parse_exinit(xi);
}
