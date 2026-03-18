#ifndef VI_H
#define VI_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <errno.h>
#include <ctype.h>
#include <stdarg.h>
#include <sys/stat.h>

#define MAX_LINES      65536
#define LINE_INIT      128
#define NUM_NAMED_BUFS 26
#define MAX_MAPS       128

#define KEY_UP    1000
#define KEY_DOWN  1001
#define KEY_RIGHT 1002
#define KEY_LEFT  1003
#define KEY_HOME  1004
#define KEY_END   1005
#define KEY_PPAGE 1006
#define KEY_NPAGE 1007
#define KEY_DEL   1008

typedef struct {
    char *data;
    int   len;
    int   cap;
} Line;

typedef enum {
    MODE_NORMAL,
    MODE_INSERT,
    MODE_REPLACE,
    MODE_EX,
    MODE_SEARCH,
    MODE_BANG
} Mode;

typedef enum {
    WAIT_NONE,
    WAIT_YY,
    WAIT_DD,
    WAIT_BUFFER,
    WAIT_ZZ,
    WAIT_Z,
    WAIT_GG,
    WAIT_REPLACE_CHAR,
    WAIT_MARK_SET,
    WAIT_MARK_GOTO,
    WAIT_BANG_MOTION
} WaitState;

typedef struct {
    char *b;
    int   len;
} Abuf;

typedef struct {
    char lhs[32];
    char rhs[256];
    int  noremap;
    int  for_insert;
} KeyMap;

typedef struct {
    char name[16];
    char value[32];
} ColorEntry;

typedef struct {
    Line           lines[MAX_LINES];
    int            nlines;
    int            cx, cy;
    int            rowoff, coloff;
    int            rows, cols;
    struct termios orig_termios;
    Mode           mode;

    char           ex_buf[256];
    int            ex_len;
    char           search_buf[256];
    int            search_len;
    int            search_dir;

    char           bang_buf[256];
    int            bang_len;
    int            bang_line1;
    int            bang_line2;

    int            dirty;
    int            readonly;
    char           filename[256];
    char           statusmsg[512];

    volatile int   sig_winch;
    volatile int   sig_term;
    volatile int   sig_hup;
    volatile int   sig_int;
    volatile int   sig_cont;
    volatile int   sig_alrm;

    char          *yank_data;
    int            yank_len;
    char         **undo_data;
    int           *undo_lens;
    int            undo_nlines;
    int            undo_cx, undo_cy;
    int            undo_valid;

    int            pending_buf;
    WaitState      wait_state;
    int            wait_count;
    int            count;
    int            count_started;

    char          *nbuf_data[NUM_NAMED_BUFS];
    int            nbuf_len[NUM_NAMED_BUFS];

    int            literal_next;
    int            ctrl_x_hex;
    int            ctrl_x_digits;

    char           ins_replay[1024];
    int            ins_replay_len;
    char           ins_current[1024];
    int            ins_current_len;

    char           term_name[64];
    char           shell[256];
    int            shiftwidth;
    int            tabstop;

    int            ex_mode;
    int            batch_mode;

    char           last_sub_pat[256];
    int            last_sub_pat_len;
    char           last_sub_rep[256];
    int            last_sub_rep_len;
    int            last_sub_global;

    char           last_bang_cmd[256];

    int            opt_autoindent;
    int            opt_syntax;
    int            opt_number;
    int            opt_ruler;
    int            opt_showmode;
    int            opt_colors;
    char           opt_statusfmt[256];
    char           opt_indentchar;
    int            opt_expandtab;
    char           filetype[32];

    KeyMap         maps[MAX_MAPS];
    int            nmap;

    ColorEntry     colors[64];
    int            ncolors;

    char           map_pending[64];
    int            map_pending_len;
} Editor;

extern Editor E;

void enable_raw(void);
void disable_raw_mode(void);
void reenable_raw_mode(void);
void get_window_size(void);
int  read_key(void);

void line_insert_char(Line *l, int pos, char c);
void line_delete_char(Line *l, int pos);
void insert_line_at(int at);
void merge_lines(int upper, int lower);
void load_file(const char *fname);
void save_file(const char *fname);
void save_undo(void);
void do_undo(void);

void ab_append(Abuf *ab, const char *s, int len);
void ab_free(Abuf *ab);
void draw_screen(void);

void clamp_cursor(void);
void process_normal(int c);
void process_insert(int c);
void process_ex_key(int c);
void process_search_key(int c);
void process_bang_key(int c);

void setup_signals(void);
void check_signals(void);

void read_env(void);
void run_ex_line(const char *s);
void source_exrc(const char *path);
void detect_filetype(void);

void dispatch_ex_cmd(const char *cmd);
void run_ex_mode(void);

int  expand_cmd(const char *src, char *dst, int dstsz);
void shell_exec(const char *cmd);
void shell_read(int after_line, const char *cmd);
void shell_filter(int line1, int line2, const char *cmd);

void highlight_line(Abuf *ab, const char *data, int len, int row);

int  map_add(const char *lhs, const char *rhs, int noremap, int for_insert);
void map_remove(const char *lhs, int for_insert);
const char *map_lookup(const char *lhs, int for_insert);
void map_show(int for_insert);

const char *color_get(const char *name);
void        color_set(const char *name, const char *value);

int  file_is_dir(const char *path);
int  file_is_readable(const char *path);
int  file_is_writable(const char *path);

#endif
