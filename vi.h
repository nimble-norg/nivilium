#ifndef VI_H
#define VI_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <signal.h>

#define MAX_LINES  65536
#define LINE_INIT  128

#define KEY_UP     1000
#define KEY_DOWN   1001
#define KEY_RIGHT  1002
#define KEY_LEFT   1003

typedef struct {
    char *data;
    int   len;
    int   cap;
} Line;

typedef enum {
    MODE_NORMAL,
    MODE_INSERT,
    MODE_EX,
    MODE_SEARCH
} Mode;

typedef struct {
    char *b;
    int   len;
} Abuf;

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
    int            dirty;
    char           filename[256];
    char           statusmsg[512];
    volatile int   need_resize;
    char          *yank_data;
    int            yank_len;
    char         **undo_data;
    int           *undo_lens;
    int            undo_nlines;
    int            undo_cx, undo_cy;
    int            undo_valid;
} Editor;

extern Editor E;

void enable_raw(void);
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

#endif
