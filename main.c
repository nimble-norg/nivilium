#include "vi.h"

Editor E;

int main(int argc, char *argv[])
{
    enable_raw();
    get_window_size();

    if (argc >= 2) {
        strncpy(E.filename, argv[1], sizeof(E.filename) - 1);
        load_file(E.filename);
    } else {
        insert_line_at(0);
    }

    while (1) {
        draw_screen();
        int c = read_key();
        switch (E.mode) {
            case MODE_NORMAL: process_normal(c);     break;
            case MODE_INSERT: process_insert(c);     break;
            case MODE_EX:     process_ex_key(c);     break;
            case MODE_SEARCH: process_search_key(c); break;
        }
    }

    return 0;
}
