#include "vi.h"
#include <strings.h>

int map_add(const char *lhs, const char *rhs, int noremap, int for_insert)
{
    if (!lhs || !*lhs || !rhs) return 0;

    for (int i = 0; i < E.nmap; i++) {
        if (E.maps[i].for_insert == for_insert &&
            strcmp(E.maps[i].lhs, lhs) == 0) {
            strncpy(E.maps[i].rhs, rhs, sizeof(E.maps[i].rhs) - 1);
            E.maps[i].rhs[sizeof(E.maps[i].rhs) - 1] = '\0';
            E.maps[i].noremap = noremap;
            return 1;
        }
    }

    if (E.nmap >= MAX_MAPS) return 0;

    strncpy(E.maps[E.nmap].lhs, lhs, sizeof(E.maps[E.nmap].lhs) - 1);
    E.maps[E.nmap].lhs[sizeof(E.maps[E.nmap].lhs) - 1] = '\0';
    strncpy(E.maps[E.nmap].rhs, rhs, sizeof(E.maps[E.nmap].rhs) - 1);
    E.maps[E.nmap].rhs[sizeof(E.maps[E.nmap].rhs) - 1] = '\0';
    E.maps[E.nmap].noremap    = noremap;
    E.maps[E.nmap].for_insert = for_insert;
    E.nmap++;
    return 1;
}

void map_remove(const char *lhs, int for_insert)
{
    for (int i = 0; i < E.nmap; i++) {
        if (E.maps[i].for_insert == for_insert &&
            strcmp(E.maps[i].lhs, lhs) == 0) {
            memmove(&E.maps[i], &E.maps[i + 1],
                    sizeof(KeyMap) * (E.nmap - i - 1));
            E.nmap--;
            return;
        }
    }
}

const char *map_lookup(const char *lhs, int for_insert)
{
    for (int i = 0; i < E.nmap; i++) {
        if (E.maps[i].for_insert == for_insert &&
            strcmp(E.maps[i].lhs, lhs) == 0)
            return E.maps[i].rhs;
    }
    return NULL;
}

void map_show(int for_insert)
{
    int shown = 0;
    for (int i = 0; i < E.nmap; i++) {
        if (E.maps[i].for_insert != for_insert) continue;
        char buf[512];
        snprintf(buf, sizeof(buf), "%s%s -> %s",
                 E.maps[i].noremap ? "noremap " : "",
                 E.maps[i].lhs, E.maps[i].rhs);
        if (E.ex_mode) {
            write(STDOUT_FILENO, buf, strlen(buf));
            write(STDOUT_FILENO, "\n", 1);
        } else {
            snprintf(E.statusmsg, sizeof(E.statusmsg), "%s", buf);
        }
        shown++;
    }
    if (!shown) {
        const char *msg = for_insert ? "No insert maps defined"
                                     : "No maps defined";
        if (E.ex_mode) {
            write(STDOUT_FILENO, msg, strlen(msg));
            write(STDOUT_FILENO, "\n", 1);
        } else {
            strncpy(E.statusmsg, msg, sizeof(E.statusmsg) - 1);
        }
    }
}

static const char *parse_keyname(const char *s, char *out, int outsz)
{
    if (s[0] == '<') {
        const char *end = strchr(s + 1, '>');
        if (end) {
            int  len = (int)(end - s - 1);
            char name[32];
            if (len < (int)sizeof(name)) {
                memcpy(name, s + 1, len);
                name[len] = '\0';
                int code = -1;
                if (!strcasecmp(name, "Up"))       code = KEY_UP;
                else if (!strcasecmp(name, "Down"))  code = KEY_DOWN;
                else if (!strcasecmp(name, "Left"))  code = KEY_LEFT;
                else if (!strcasecmp(name, "Right")) code = KEY_RIGHT;
                else if (!strcasecmp(name, "Home"))  code = KEY_HOME;
                else if (!strcasecmp(name, "End"))   code = KEY_END;
                else if (!strcasecmp(name, "PageUp") ||
                         !strcasecmp(name, "PgUp"))  code = KEY_PPAGE;
                else if (!strcasecmp(name, "PageDown") ||
                         !strcasecmp(name, "PgDn"))  code = KEY_NPAGE;
                else if (!strcasecmp(name, "Del"))   code = KEY_DEL;
                else if (!strcasecmp(name, "Esc") ||
                         !strcasecmp(name, "Escape")) code = 27;
                else if (!strcasecmp(name, "CR") ||
                         !strcasecmp(name, "Enter")) code = 13;
                else if (!strcasecmp(name, "BS") ||
                         !strcasecmp(name, "BackSpace")) code = 127;
                else if (!strcasecmp(name, "Tab"))   code = 9;
                else if (!strcasecmp(name, "Space")) code = 32;
                else if (len >= 2 && (name[0] == 'C' || name[0] == 'c') &&
                         name[1] == '-' && name[2]) {
                    code = toupper((unsigned char)name[2]) - '@';
                }

                if (code >= 0 && outsz >= 5) {
                    out[0] = '\x01';
                    out[1] = (char)((code >> 8) & 0xff);
                    out[2] = (char)(code & 0xff);
                    out[3] = '\0';
                    return end + 1;
                }
            }
        }
    }

    if (outsz > 1) { *out = *s; out[1] = '\0'; }
    return s + 1;
}

void map_parse_lhs(const char *src, char *dst, int dstsz)
{
    int di = 0;
    while (*src && di < dstsz - 4) {
        char tmp[8];
        src = parse_keyname(src, tmp, sizeof(tmp));
        int l = (int)strlen(tmp);
        if (di + l < dstsz - 1) { memcpy(dst + di, tmp, l); di += l; }
    }
    dst[di] = '\0';
}

int map_key_sequence(const char *seq, int seqlen, int for_insert,
                     char *out_rhs, int *out_consumed)
{
    int best_len = 0;
    const char *best_rhs = NULL;

    for (int i = 0; i < E.nmap; i++) {
        if (E.maps[i].for_insert != for_insert) continue;
        int  klen = (int)strlen(E.maps[i].lhs);
        if (klen > seqlen) continue;
        if (memcmp(E.maps[i].lhs, seq, klen) == 0) {
            if (klen > best_len) {
                best_len = klen;
                best_rhs = E.maps[i].rhs;
            }
        }
    }

    if (best_rhs) {
        strncpy(out_rhs, best_rhs, 255);
        out_rhs[255] = '\0';
        *out_consumed = best_len;
        return 1;
    }
    return 0;
}
