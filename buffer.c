#include "vi.h"

int file_is_dir(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) return 1;
    return 0;
}

int file_is_readable(const char *path)
{
    return access(path, R_OK) == 0;
}

int file_is_writable(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0) {
        if (access(path, W_OK) != 0) return 0;
        return 1;
    }
    return 1;
}

void line_insert_char(Line *l, int pos, char c)
{
    if (l->len + 2 > l->cap) {
        l->cap  = l->cap ? l->cap * 2 : LINE_INIT;
        l->data = realloc(l->data, l->cap);
    }
    memmove(l->data + pos + 1, l->data + pos, l->len - pos);
    l->data[pos] = c;
    l->len++;
}

void line_delete_char(Line *l, int pos)
{
    if (pos < 0 || pos >= l->len) return;
    memmove(l->data + pos, l->data + pos + 1, l->len - pos - 1);
    l->len--;
}

void insert_line_at(int at)
{
    if (E.nlines >= MAX_LINES) return;
    memmove(&E.lines[at + 1], &E.lines[at],
            sizeof(Line) * (E.nlines - at));
    E.lines[at].data = malloc(LINE_INIT);
    E.lines[at].len  = 0;
    E.lines[at].cap  = LINE_INIT;
    E.nlines++;
}

void merge_lines(int upper, int lower)
{
    Line *u    = &E.lines[upper];
    Line *l    = &E.lines[lower];
    int   need = u->len + l->len + 1;

    if (need > u->cap) {
        u->cap  = need + LINE_INIT;
        u->data = realloc(u->data, u->cap);
    }
    memcpy(u->data + u->len, l->data, l->len);
    u->len += l->len;

    free(l->data);
    memmove(&E.lines[lower], &E.lines[lower + 1],
            sizeof(Line) * (E.nlines - lower - 1));
    E.nlines--;
}

void save_undo(void)
{
    if (E.undo_data) {
        for (int i = 0; i < E.undo_nlines; i++) free(E.undo_data[i]);
        free(E.undo_data);
        free(E.undo_lens);
    }
    E.undo_data   = malloc(sizeof(char *) * E.nlines);
    E.undo_lens   = malloc(sizeof(int)    * E.nlines);
    E.undo_nlines = E.nlines;
    E.undo_cx     = E.cx;
    E.undo_cy     = E.cy;
    E.undo_valid  = 1;
    for (int i = 0; i < E.nlines; i++) {
        E.undo_data[i] = malloc(E.lines[i].len + 1);
        memcpy(E.undo_data[i], E.lines[i].data, E.lines[i].len);
        E.undo_lens[i] = E.lines[i].len;
    }
}

void do_undo(void)
{
    if (!E.undo_valid) {
        snprintf(E.statusmsg, sizeof(E.statusmsg), "Already at oldest change");
        return;
    }
    for (int i = 0; i < E.nlines; i++) free(E.lines[i].data);
    E.nlines = E.undo_nlines;
    for (int i = 0; i < E.nlines; i++) {
        E.lines[i].cap  = E.undo_lens[i] + LINE_INIT;
        E.lines[i].data = malloc(E.lines[i].cap);
        memcpy(E.lines[i].data, E.undo_data[i], E.undo_lens[i]);
        E.lines[i].len  = E.undo_lens[i];
    }
    E.cx         = E.undo_cx;
    E.cy         = E.undo_cy;
    E.dirty      = 1;
    E.undo_valid = 0;
    snprintf(E.statusmsg, sizeof(E.statusmsg), "1 change; before #1");
}

void load_file(const char *fname)
{
    if (file_is_dir(fname)) {
        insert_line_at(0);
        snprintf(E.statusmsg, sizeof(E.statusmsg),
                 "E17: \"%s\" is a directory", fname);
        return;
    }

    struct stat st;
    if (stat(fname, &st) == 0) {
        if (access(fname, R_OK) != 0) {
            insert_line_at(0);
            snprintf(E.statusmsg, sizeof(E.statusmsg),
                     "E484: Can't open file \"%s\": permission denied", fname);
            E.readonly = 1;
            return;
        }
        if (access(fname, W_OK) != 0) {
            E.readonly = 1;
        }
    }

    FILE *f = fopen(fname, "r");
    if (!f) {
        insert_line_at(0);
        if (errno == ENOENT) {
            snprintf(E.statusmsg, sizeof(E.statusmsg),
                     "\"%s\" [New File]", fname);
        } else {
            snprintf(E.statusmsg, sizeof(E.statusmsg),
                     "E484: Can't open file \"%s\": %s", fname, strerror(errno));
            E.readonly = 1;
        }
        return;
    }

    char buf[4096];
    while (fgets(buf, sizeof(buf), f)) {
        int l = (int)strlen(buf);
        if (l > 0 && buf[l - 1] == '\n') buf[--l] = '\0';
        insert_line_at(E.nlines);
        if (l > 0) {
            Line *cur = &E.lines[E.nlines - 1];
            if (l + 1 > cur->cap) {
                cur->cap  = l + LINE_INIT;
                cur->data = realloc(cur->data, cur->cap);
            }
            memcpy(cur->data, buf, l);
            cur->len = l;
        }
    }
    fclose(f);

    if (E.nlines == 0) insert_line_at(0);

    const char *ro = E.readonly ? " [readonly]" : "";
    snprintf(E.statusmsg, sizeof(E.statusmsg),
             "\"%s\"%s %dL", fname, ro, E.nlines);
}

void save_file(const char *fname)
{
    if (E.readonly) {
        snprintf(E.statusmsg, sizeof(E.statusmsg),
                 "E45: 'readonly' option is set (use ! to override)");
        return;
    }

    if (file_is_dir(fname)) {
        snprintf(E.statusmsg, sizeof(E.statusmsg),
                 "E17: \"%s\" is a directory — use :w <filename> to save", fname);
        return;
    }

    struct stat st;
    if (stat(fname, &st) == 0 && access(fname, W_OK) != 0) {
        snprintf(E.statusmsg, sizeof(E.statusmsg),
                 "E505: \"%s\" is read-only (use ! to override)", fname);
        return;
    }

    FILE *f = fopen(fname, "w");
    if (!f) {
        snprintf(E.statusmsg, sizeof(E.statusmsg),
                 "E212: Can't open \"%s\" for writing: %s",
                 fname, strerror(errno));
        return;
    }
    for (int i = 0; i < E.nlines; i++) {
        fwrite(E.lines[i].data, 1, E.lines[i].len, f);
        fwrite("\n", 1, 1, f);
    }
    fclose(f);
    E.dirty = 0;
    snprintf(E.statusmsg, sizeof(E.statusmsg),
             "\"%s\" %dL written", fname, E.nlines);
}
