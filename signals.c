#include "vi.h"

static void sigwinch_handler(int s) { (void)s; E.sig_winch = 1; }
static void sigterm_handler(int s)  { (void)s; E.sig_term  = 1; }
static void sighup_handler(int s)   { (void)s; E.sig_hup   = 1; }
static void sigint_handler(int s)   { (void)s; E.sig_int   = 1; }
static void sigcont_handler(int s)  { (void)s; E.sig_cont  = 1; }
static void sigalrm_handler(int s)  { (void)s; E.sig_alrm  = 1; }

static void sigtstp_handler(int s)
{
    (void)s;
    disable_raw_mode();
    signal(SIGTSTP, SIG_DFL);
    raise(SIGTSTP);
}

static void install_sigtstp(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = sigtstp_handler;
    sigaction(SIGTSTP, &sa, NULL);
}

void swap_path(const char *fname, char *out, int outsz)
{
    if (!fname || !fname[0]) {
        snprintf(out, outsz, ".vi.swp");
        return;
    }
    const char *slash = strrchr(fname, '/');
    if (slash) {
        int dirlen = (int)(slash - fname + 1);
        if (dirlen >= outsz - 1) dirlen = outsz - 2;
        memcpy(out, fname, dirlen);
        out[dirlen] = '\0';
        const char *base = slash + 1;
        snprintf(out + dirlen, outsz - dirlen, ".%s.swp", base);
    } else {
        snprintf(out, outsz, ".%s.swp", fname);
    }
}

void remove_swap(void)
{
    if (!E.filename[0]) return;
    char path[512];
    swap_path(E.filename, path, sizeof(path));
    unlink(path);
}

void setup_signals(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sigemptyset(&sa.sa_mask);

    sa.sa_handler = sigwinch_handler; sigaction(SIGWINCH, &sa, NULL);
    sa.sa_handler = sigterm_handler;  sigaction(SIGTERM,  &sa, NULL);
    sa.sa_handler = sighup_handler;   sigaction(SIGHUP,   &sa, NULL);
    sa.sa_handler = sigint_handler;   sigaction(SIGINT,   &sa, NULL);
    sa.sa_handler = sigcont_handler;  sigaction(SIGCONT,  &sa, NULL);
    sa.sa_handler = sigalrm_handler;  sigaction(SIGALRM,  &sa, NULL);
    sa.sa_handler = SIG_IGN;          sigaction(SIGPIPE,  &sa, NULL);
    install_sigtstp();
    alarm(30);
}

static void backup_file(void)
{
    if (!E.dirty || !E.filename[0] || E.readonly) return;
    char rec[512];
    swap_path(E.filename, rec, sizeof(rec));
    FILE *f = fopen(rec, "w");
    if (!f) return;
    for (int i = 0; i < E.nlines; i++) {
        fwrite(E.lines[i].data, 1, E.lines[i].len, f);
        fwrite("\n", 1, 1, f);
    }
    fclose(f);
}

void clean_exit_editor(void)
{
    remove_swap();
    exit(0);
}

void check_signals(void)
{
    if (E.sig_winch) {
        E.sig_winch = 0;
        get_window_size();
        draw_screen();
    }
    if (E.sig_cont) {
        E.sig_cont = 0;
        install_sigtstp();
        reenable_raw_mode();
        get_window_size();
        draw_screen();
    }
    if (E.sig_alrm) {
        E.sig_alrm = 0;
        alarm(30);
        backup_file();
    }
    if (E.sig_int) {
        E.sig_int       = 0;
        E.count         = 0;
        E.count_started = 0;
        E.pending_buf   = -1;
        E.wait_state    = WAIT_NONE;
        E.literal_next  = 0;
        E.ctrl_x_digits = 0;
        if (E.mode == MODE_INSERT || E.mode == MODE_REPLACE) {
            if (E.ins_current_len > 0) {
                int n = E.ins_current_len < (int)sizeof(E.ins_replay) - 1
                        ? E.ins_current_len : (int)sizeof(E.ins_replay) - 1;
                memcpy(E.ins_replay, E.ins_current, n);
                E.ins_replay_len = n;
            }
            E.ins_current_len = 0;
        }
        E.mode = MODE_NORMAL;
        clamp_cursor();
        E.statusmsg[0]     = '\0';
        E.statusmsg_err    = 0;
    }
    if (E.sig_term) {
        backup_file();
        exit(1);
    }
    if (E.sig_hup) {
        backup_file();
        exit(1);
    }
}

