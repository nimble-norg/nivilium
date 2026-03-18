#include "vi.h"

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
    Line *u = &E.lines[upper];
    Line *l = &E.lines[lower];
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
    FILE *f = fopen(fname, "r");
    if (!f) {
        insert_line_at(0);
        snprintf(E.statusmsg, sizeof(E.statusmsg),
                 "\"%s\" [New File]", fname);
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

    snprintf(E.statusmsg, sizeof(E.statusmsg),
             "\"%s\" %dL", fname, E.nlines);
}

void save_file(const char *fname)
{
    if (E.readonly) {
        snprintf(E.statusmsg, sizeof(E.statusmsg),
                 "E45: 'readonly' option is set (use ! to override)");
        return;
    }
    FILE *f = fopen(fname, "w");
    if (!f) {
        snprintf(E.statusmsg, sizeof(E.statusmsg),
                 "E212: Cannot open \"%s\" for writing", fname);
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
