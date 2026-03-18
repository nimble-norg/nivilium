#include "vi.h"
#include <sys/types.h>

Editor E;

static const char *basename_of(const char *path)
{
    const char *p = strrchr(path, '/');
    return p ? p + 1 : path;
}

static void print_version(void)
{
    const char *msg =
        "@(#)NIMBLE-ORG NIVILIUM " __DATE__ ", " __TIME__ "\n";
    write(STDOUT_FILENO, msg, strlen(msg));
    exit(0);
}

int main(int argc, char *argv[])
{
    int         start_ex   = 0;
    int         start_ro   = 0;
    int         force_vi   = 0;
    int         batch      = 0;
    const char *init_cmd   = NULL;
    const char *tag_str    = NULL;
    const char *fname      = NULL;
    int         recover    = 0;

    const char *base = basename_of(argv[0]);
    if (strcmp(base, "ex")   == 0) start_ex = 1;
    if (strcmp(base, "view") == 0) start_ro = 1;

    int i = 1;
    for (; i < argc; i++) {
        if (strcmp(argv[i], "--version") == 0) print_version();
        if (strcmp(argv[i], "--ex")      == 0) { start_ex = 1; continue; }
        if (strcmp(argv[i], "--")        == 0) { i++; break; }

        if (argv[i][0] == '+') {
            init_cmd = argv[i] + 1;
            continue;
        }

        if (argv[i][0] != '-') break;

        const char *p = argv[i] + 1;
        while (*p) {
            switch (*p) {
            case 'e': start_ex = 1; break;
            case 'v': force_vi = 1; break;
            case 'R': start_ro = 1; break;
            case 'r': recover  = 1; break;
            case 's': batch    = 1; break;
            case 'S': break;
            case 'F': break;
            case 'c':
                p++;
                if (*p) {
                    init_cmd  = p;
                    p        += strlen(p) - 1;
                } else if (i + 1 < argc) {
                    init_cmd = argv[++i];
                }
                break;
            case 't':
                p++;
                if (*p) {
                    tag_str = p;
                    p      += strlen(p) - 1;
                } else if (i + 1 < argc) {
                    tag_str = argv[++i];
                }
                break;
            case 'w':
                if (*(p + 1)) p += strlen(p + 1);
                else          i++;
                break;
            default: break;
            }
            p++;
        }
    }

    if (force_vi) start_ex = 0;
    if (i < argc) fname = argv[i];

    E.ex_mode  = start_ex;
    E.readonly = start_ro;

    if (!E.ex_mode) {
        if (!isatty(STDIN_FILENO)) {
            write(STDERR_FILENO,
                  "vi: standard input is not a terminal\n", 37);
            exit(1);
        }
        if (!isatty(STDOUT_FILENO)) {
            write(STDERR_FILENO,
                  "vi: standard output is not a terminal\n", 38);
            exit(1);
        }
    } else {
        if (!isatty(STDIN_FILENO) || batch)
            E.batch_mode = 1;
    }

    if (!E.ex_mode) {
        enable_raw();
        get_window_size();
    }

    read_env();
    setup_signals();

    if (fname) {
        if (file_is_dir(fname)) {
            if (!E.ex_mode) {
                snprintf(E.statusmsg, sizeof(E.statusmsg),
                         "E17: \"%s\" is a directory", fname);
                insert_line_at(0);
            } else {
                write(STDERR_FILENO, "vi: \"", 5);
                write(STDERR_FILENO, fname, strlen(fname));
                write(STDERR_FILENO, "\" is a directory\n", 17);
                exit(1);
            }
        } else {
            strncpy(E.filename, fname, sizeof(E.filename) - 1);
            detect_filetype();
            load_file(E.filename);
            if (recover) {
                char swp[512];
                snprintf(swp, sizeof(swp), "%s.swp", E.filename);
                if (access(swp, R_OK) == 0) {
                    for (int k = 0; k < E.nlines; k++) free(E.lines[k].data);
                    E.nlines = 0; E.cx = 0; E.cy = 0;
                    load_file(swp);
                    snprintf(E.statusmsg, sizeof(E.statusmsg),
                             "Recovered from \"%.*s\"",
                             (int)(sizeof(E.statusmsg) - 20), swp);
                }
            }
        }
    } else if (tag_str) {
        snprintf(E.statusmsg, sizeof(E.statusmsg),
                 "E433: No tags file: %s", tag_str);
        insert_line_at(0);
    } else {
        insert_line_at(0);
    }

    if (init_cmd) {
        char cbuf[256];
        int  all_digits = 1;
        for (int j = 0; init_cmd[j]; j++)
            if (!isdigit((unsigned char)init_cmd[j])) { all_digits = 0; break; }
        if (all_digits && init_cmd[0]) {
            int line = atoi(init_cmd);
            E.cy = line > 0 ? line - 1 : 0;
            if (E.cy >= E.nlines) E.cy = E.nlines > 0 ? E.nlines - 1 : 0;
        } else if (init_cmd[0] == '$') {
            E.cy = E.nlines > 0 ? E.nlines - 1 : 0;
        } else {
            snprintf(cbuf, sizeof(cbuf), "%s", init_cmd);
            dispatch_ex_cmd(cbuf);
        }
    }

    if (E.ex_mode) {
        run_ex_mode();
        if (E.ex_mode) exit(0);
        enable_raw();
        get_window_size();
    }

    while (1) {
        check_signals();
        draw_screen();
        int c = read_key();
        switch (E.mode) {
            case MODE_NORMAL:  process_normal(c);     break;
            case MODE_INSERT:
            case MODE_REPLACE: process_insert(c);     break;
            case MODE_EX:      process_ex_key(c);     break;
            case MODE_SEARCH:  process_search_key(c); break;
            case MODE_BANG:    process_bang_key(c);   break;
        }
    }

    return 0;
}
