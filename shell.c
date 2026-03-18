#include "vi.h"
#include <sys/wait.h>
#include <fcntl.h>

int expand_cmd(const char *src, char *dst, int dstsz)
{
    int  si = 0, di = 0;
    int  ok = 1;

    while (src[si] && di < dstsz - 1) {
        if (src[si] == '\\' && (src[si + 1] == '%' || src[si + 1] == '!')) {
            si++;
            dst[di++] = src[si++];
            continue;
        }

        if (src[si] == '%') {
            if (!E.filename[0]) {
                strncpy(E.statusmsg,
                        "E499: Empty file name for '%' or '#'",
                        sizeof(E.statusmsg) - 1);
                ok = 0;
                break;
            }
            int flen = (int)strlen(E.filename);
            if (di + flen >= dstsz - 1) { ok = 0; break; }
            memcpy(dst + di, E.filename, flen);
            di += flen;
            si++;
            continue;
        }

        if (src[si] == '!' && si > 0) {
            if (!E.last_bang_cmd[0]) {
                strncpy(E.statusmsg,
                        "E34: No previous command",
                        sizeof(E.statusmsg) - 1);
                ok = 0;
                break;
            }
            int blen = (int)strlen(E.last_bang_cmd);
            if (di + blen >= dstsz - 1) { ok = 0; break; }
            memcpy(dst + di, E.last_bang_cmd, blen);
            di += blen;
            si++;
            continue;
        }

        dst[di++] = src[si++];
    }

    dst[di] = '\0';
    return ok;
}

static void write_all(int fd, const char *buf, int len)
{
    int written = 0;
    while (written < len) {
        ssize_t n = write(fd, buf + written, len - written);
        if (n <= 0) break;
        written += (int)n;
    }
}

static void pause_for_enter(void)
{
    const char *msg = "\r\n[Press ENTER to continue]";
    write_all(STDOUT_FILENO, msg, (int)strlen(msg));
    char c;
    while (read(STDIN_FILENO, &c, 1) == 1) {
        if (c == '\r' || c == '\n' || c == ' ') break;
    }
}

void shell_exec(const char *cmd)
{
    if (!cmd || !*cmd) return;
    strncpy(E.last_bang_cmd, cmd, sizeof(E.last_bang_cmd) - 1);
    E.last_bang_cmd[sizeof(E.last_bang_cmd) - 1] = '\0';

    if (!E.ex_mode) {
        disable_raw_mode();
        write_all(STDOUT_FILENO, "\r\n", 2);
    }

    const char *argv[4];
    argv[0] = E.shell[0] ? E.shell : "/bin/sh";
    argv[1] = "-c";
    argv[2] = cmd;
    argv[3] = NULL;

    pid_t pid = fork();
    if (pid == 0) {
        execv(argv[0], (char *const *)argv);
        _exit(127);
    } else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
        if (!E.ex_mode) {
            pause_for_enter();
            reenable_raw_mode();
            get_window_size();
            draw_screen();
        }
    } else {
        strncpy(E.statusmsg, "E605: Exception not caught: fork failed",
                sizeof(E.statusmsg) - 1);
        if (!E.ex_mode) reenable_raw_mode();
    }
}

void shell_read(int after_line, const char *cmd)
{
    if (!cmd || !*cmd) return;
    strncpy(E.last_bang_cmd, cmd, sizeof(E.last_bang_cmd) - 1);
    E.last_bang_cmd[sizeof(E.last_bang_cmd) - 1] = '\0';

    int pipefd[2];
    if (pipe(pipefd) == -1) {
        strncpy(E.statusmsg, "E605: pipe failed", sizeof(E.statusmsg) - 1);
        return;
    }

    pid_t pid = fork();
    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        const char *sh = E.shell[0] ? E.shell : "/bin/sh";
        execl(sh, sh, "-c", cmd, (char *)NULL);
        _exit(127);
    } else if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        strncpy(E.statusmsg, "E605: fork failed", sizeof(E.statusmsg) - 1);
        return;
    }

    close(pipefd[1]);

    char   buf[4096];
    int    ins    = after_line;
    int    nlines = 0;
    char   line[8192];
    int    llen = 0;
    ssize_t n;

    save_undo();

    while ((n = read(pipefd[0], buf, sizeof(buf))) > 0) {
        for (ssize_t i = 0; i < n; i++) {
            if (buf[i] == '\n') {
                insert_line_at(ins + 1);
                ins++;
                Line *l = &E.lines[ins];
                if (llen + 1 > l->cap) {
                    l->cap  = llen + LINE_INIT;
                    l->data = realloc(l->data, l->cap);
                }
                memcpy(l->data, line, llen);
                l->len = llen;
                llen   = 0;
                nlines++;
            } else {
                if (llen < (int)sizeof(line) - 1)
                    line[llen++] = buf[i];
            }
        }
    }

    if (llen > 0) {
        insert_line_at(ins + 1);
        ins++;
        Line *l = &E.lines[ins];
        if (llen + 1 > l->cap) {
            l->cap  = llen + LINE_INIT;
            l->data = realloc(l->data, l->cap);
        }
        memcpy(l->data, line, llen);
        l->len = llen;
        nlines++;
    }

    close(pipefd[0]);

    int status;
    waitpid(pid, &status, 0);

    E.cy    = ins > 0 ? ins : 0;
    E.dirty = (nlines > 0);
    clamp_cursor();

    snprintf(E.statusmsg, sizeof(E.statusmsg),
             "%d line%s", nlines, nlines != 1 ? "s" : "");
}

void shell_filter(int line1, int line2, const char *cmd)
{
    if (!cmd || !*cmd) return;
    strncpy(E.last_bang_cmd, cmd, sizeof(E.last_bang_cmd) - 1);
    E.last_bang_cmd[sizeof(E.last_bang_cmd) - 1] = '\0';

    int in_pipe[2], out_pipe[2];
    if (pipe(in_pipe) == -1) {
        strncpy(E.statusmsg, "E605: pipe failed", sizeof(E.statusmsg) - 1);
        return;
    }
    if (pipe(out_pipe) == -1) {
        close(in_pipe[0]);
        close(in_pipe[1]);
        strncpy(E.statusmsg, "E605: pipe failed", sizeof(E.statusmsg) - 1);
        return;
    }

    pid_t pid = fork();
    if (pid == 0) {
        close(in_pipe[1]);
        close(out_pipe[0]);
        dup2(in_pipe[0],  STDIN_FILENO);
        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(out_pipe[1], STDERR_FILENO);
        close(in_pipe[0]);
        close(out_pipe[1]);
        const char *sh = E.shell[0] ? E.shell : "/bin/sh";
        execl(sh, sh, "-c", cmd, (char *)NULL);
        _exit(127);
    } else if (pid < 0) {
        close(in_pipe[0]);  close(in_pipe[1]);
        close(out_pipe[0]); close(out_pipe[1]);
        strncpy(E.statusmsg, "E605: fork failed", sizeof(E.statusmsg) - 1);
        return;
    }

    close(in_pipe[0]);
    close(out_pipe[1]);

    for (int i = line1; i <= line2 && i < E.nlines; i++) {
        write_all(in_pipe[1], E.lines[i].data, E.lines[i].len);
        write_all(in_pipe[1], "\n", 1);
    }
    close(in_pipe[1]);

    char   ibuf[4096];
    char   lbuf[8192];
    int    llen  = 0;
    int    ins   = line1 - 1;
    int    nnew  = 0;
    ssize_t n;

    while ((n = read(out_pipe[0], ibuf, sizeof(ibuf))) > 0) {
        for (ssize_t i = 0; i < n; i++) {
            if (ibuf[i] == '\n') {
                ins++;
                if (nnew < (line2 - line1 + 1)) {
                    Line *l = &E.lines[line1 + nnew];
                    if (llen + 1 > l->cap) {
                        l->cap  = llen + LINE_INIT;
                        l->data = realloc(l->data, l->cap);
                    }
                    memcpy(l->data, lbuf, llen);
                    l->len = llen;
                } else {
                    insert_line_at(line1 + nnew);
                    Line *l = &E.lines[line1 + nnew];
                    if (llen + 1 > l->cap) {
                        l->cap  = llen + LINE_INIT;
                        l->data = realloc(l->data, l->cap);
                    }
                    memcpy(l->data, lbuf, llen);
                    l->len = llen;
                }
                nnew++;
                llen = 0;
            } else {
                if (llen < (int)sizeof(lbuf) - 1)
                    lbuf[llen++] = ibuf[i];
            }
        }
    }

    if (llen > 0) {
        ins++;
        if (nnew < (line2 - line1 + 1)) {
            Line *l = &E.lines[line1 + nnew];
            if (llen + 1 > l->cap) {
                l->cap  = llen + LINE_INIT;
                l->data = realloc(l->data, l->cap);
            }
            memcpy(l->data, lbuf, llen);
            l->len = llen;
        } else {
            insert_line_at(line1 + nnew);
            Line *l = &E.lines[line1 + nnew];
            if (llen + 1 > l->cap) {
                l->cap  = llen + LINE_INIT;
                l->data = realloc(l->data, l->cap);
            }
            memcpy(l->data, lbuf, llen);
            l->len = llen;
        }
        nnew++;
    }

    close(out_pipe[0]);

    int status;
    waitpid(pid, &status, 0);

    int orig = line2 - line1 + 1;
    if (nnew < orig) {
        int del = orig - nnew;
        int at  = line1 + nnew;
        for (int k = 0; k < del; k++) {
            if (at < E.nlines) {
                free(E.lines[at].data);
                memmove(&E.lines[at], &E.lines[at + 1],
                        sizeof(Line) * (E.nlines - at - 1));
                E.nlines--;
            }
        }
    }

    if (E.nlines == 0) insert_line_at(0);
    E.cy    = line1 + (nnew > 0 ? nnew - 1 : 0);
    E.dirty = 1;
    clamp_cursor();

    snprintf(E.statusmsg, sizeof(E.statusmsg),
             "%d line%s filtered", nnew, nnew != 1 ? "s" : "");
}
