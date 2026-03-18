#include "vi.h"

#define HL_RESET   "\x1b[0m"
#define HL_KW      "\x1b[1;34m"
#define HL_TYPE    "\x1b[0;36m"
#define HL_STR     "\x1b[0;32m"
#define HL_CHAR    "\x1b[0;32m"
#define HL_COMMENT "\x1b[2;37m"
#define HL_PREPROC "\x1b[0;35m"
#define HL_NUMBER  "\x1b[0;33m"

static const char *c_keywords[] = {
    "auto","break","case","const","continue","default","do","else",
    "enum","extern","for","goto","if","inline","register","return",
    "sizeof","static","struct","switch","typedef","union","volatile",
    "while", NULL
};

static const char *c_types[] = {
    "char","double","float","int","long","short","signed","unsigned",
    "void","size_t","ssize_t","ptrdiff_t","intptr_t","uintptr_t",
    "uint8_t","uint16_t","uint32_t","uint64_t",
    "int8_t","int16_t","int32_t","int64_t", NULL
};

static const char *sh_keywords[] = {
    "if","then","else","elif","fi","case","esac","for","while",
    "until","do","done","in","function","return","exit","local",
    "export","readonly","unset","shift","break","continue",
    "true","false", NULL
};

static const char *py_keywords[] = {
    "False","None","True","and","as","assert","async","await",
    "break","class","continue","def","del","elif","else","except",
    "finally","for","from","global","if","import","in","is",
    "lambda","nonlocal","not","or","pass","raise","return",
    "try","while","with","yield", NULL
};

static int kw_match(const char *data, int pos, int len,
                    const char **kws, int *klen_out)
{
    for (int k = 0; kws[k]; k++) {
        int kl = (int)strlen(kws[k]);
        if (pos + kl > len) continue;
        if (memcmp(data + pos, kws[k], kl) != 0) continue;
        int after = pos + kl;
        if (after < len && (isalnum((unsigned char)data[after])
                            || data[after] == '_'))
            continue;
        if (pos > 0 && (isalnum((unsigned char)data[pos - 1])
                        || data[pos - 1] == '_'))
            continue;
        *klen_out = kl;
        return 1;
    }
    return 0;
}

static void ab_str(Abuf *ab, const char *s)
{
    ab_append(ab, s, (int)strlen(s));
}

static void emit_c(Abuf *ab, const char *data, int len, int is_cpp)
{
    (void)is_cpp;
    int        i         = 0;
    int        in_str    = 0;
    char       str_delim = 0;
    int        in_mlc    = 0;
    int        colored   = 0;

    while (i < len) {
        if (in_mlc) {
            if (i + 1 < len && data[i] == '*' && data[i + 1] == '/') {
                ab_append(ab, data + i, 2);
                i += 2;
                ab_str(ab, HL_RESET);
                in_mlc = 0;
                colored = 0;
            } else {
                ab_append(ab, data + i, 1);
                i++;
            }
            continue;
        }

        if (!in_str && i > 0 && data[0] == '#') {
            if (!colored) {
                ab_str(ab, HL_PREPROC);
                colored = 1;
            }
            ab_append(ab, data + i, len - i);
            i = len;
            break;
        }

        if (!in_str && i + 1 < len &&
            data[i] == '/' && data[i + 1] == '*') {
            if (colored) { ab_str(ab, HL_RESET); colored = 0; }
            ab_str(ab, HL_COMMENT);
            ab_append(ab, data + i, 2);
            i += 2;
            in_mlc = 1;
            colored = 1;
            continue;
        }

        if (!in_str && i + 1 < len &&
            data[i] == '/' && data[i + 1] == '/') {
            if (colored) { ab_str(ab, HL_RESET); colored = 0; }
            ab_str(ab, HL_COMMENT);
            ab_append(ab, data + i, len - i);
            i = len;
            ab_str(ab, HL_RESET);
            break;
        }

        if (!in_str) {
            char ch = data[i];
            if (ch == '"' || ch == '\'') {
                in_str    = 1;
                str_delim = ch;
                if (colored) { ab_str(ab, HL_RESET); colored = 0; }
                ab_str(ab, HL_STR);
                ab_append(ab, data + i, 1);
                i++;
                colored = 1;
                continue;
            }

            if (isdigit((unsigned char)ch) ||
                (ch == '.' && i + 1 < len &&
                 isdigit((unsigned char)data[i + 1]))) {
                if (colored) { ab_str(ab, HL_RESET); colored = 0; }
                ab_str(ab, HL_NUMBER);
                colored = 1;
                while (i < len && (isalnum((unsigned char)data[i])
                                   || data[i] == '.' || data[i] == '_'))
                    ab_append(ab, data + i++, 1);
                ab_str(ab, HL_RESET);
                colored = 0;
                continue;
            }

            if (isalpha((unsigned char)ch) || ch == '_') {
                int kl = 0;
                if (kw_match(data, i, len, c_keywords, &kl)) {
                    if (colored) { ab_str(ab, HL_RESET); colored = 0; }
                    ab_str(ab, HL_KW);
                    ab_append(ab, data + i, kl);
                    ab_str(ab, HL_RESET);
                    i += kl;
                    colored = 0;
                    continue;
                }
                if (kw_match(data, i, len, c_types, &kl)) {
                    if (colored) { ab_str(ab, HL_RESET); colored = 0; }
                    ab_str(ab, HL_TYPE);
                    ab_append(ab, data + i, kl);
                    ab_str(ab, HL_RESET);
                    i += kl;
                    colored = 0;
                    continue;
                }
            }

            if (colored) { ab_str(ab, HL_RESET); colored = 0; }
            ab_append(ab, data + i, 1);
            i++;
            continue;
        }

        if (in_str) {
            if (data[i] == '\\' && i + 1 < len) {
                ab_append(ab, data + i, 2);
                i += 2;
                continue;
            }
            ab_append(ab, data + i, 1);
            if (data[i] == str_delim) {
                ab_str(ab, HL_RESET);
                in_str  = 0;
                colored = 0;
            }
            i++;
        }
    }

    if (colored) ab_str(ab, HL_RESET);
}

static void emit_sh(Abuf *ab, const char *data, int len)
{
    int i       = 0;
    int in_str  = 0;
    char delim  = 0;
    int colored = 0;

    while (i < len) {
        if (!in_str && data[i] == '#') {
            if (colored) { ab_str(ab, HL_RESET); colored = 0; }
            ab_str(ab, HL_COMMENT);
            ab_append(ab, data + i, len - i);
            i = len;
            ab_str(ab, HL_RESET);
            break;
        }

        if (!in_str) {
            char ch = data[i];
            if (ch == '"' || ch == '\'') {
                in_str = 1; delim = ch;
                if (colored) { ab_str(ab, HL_RESET); colored = 0; }
                ab_str(ab, HL_STR);
                ab_append(ab, data + i++, 1);
                colored = 1;
                continue;
            }

            if (isalpha((unsigned char)ch) || ch == '_') {
                int kl = 0;
                if (kw_match(data, i, len, sh_keywords, &kl)) {
                    if (colored) { ab_str(ab, HL_RESET); colored = 0; }
                    ab_str(ab, HL_KW);
                    ab_append(ab, data + i, kl);
                    ab_str(ab, HL_RESET);
                    i += kl;
                    colored = 0;
                    continue;
                }
            }

            if (colored) { ab_str(ab, HL_RESET); colored = 0; }
            ab_append(ab, data + i++, 1);
            continue;
        }

        if (in_str) {
            if (data[i] == '\\' && i + 1 < len && delim != '\'') {
                ab_append(ab, data + i, 2);
                i += 2;
                continue;
            }
            ab_append(ab, data + i, 1);
            if (data[i] == delim) {
                ab_str(ab, HL_RESET);
                in_str  = 0;
                colored = 0;
            }
            i++;
        }
    }

    if (colored) ab_str(ab, HL_RESET);
}

static void emit_py(Abuf *ab, const char *data, int len)
{
    int i       = 0;
    int in_str  = 0;
    char delim  = 0;
    int colored = 0;

    while (i < len) {
        if (!in_str && data[i] == '#') {
            if (colored) { ab_str(ab, HL_RESET); colored = 0; }
            ab_str(ab, HL_COMMENT);
            ab_append(ab, data + i, len - i);
            i = len;
            ab_str(ab, HL_RESET);
            break;
        }

        if (!in_str) {
            char ch = data[i];
            if (ch == '"' || ch == '\'') {
                in_str = 1; delim = ch;
                if (colored) { ab_str(ab, HL_RESET); colored = 0; }
                ab_str(ab, HL_STR);
                ab_append(ab, data + i++, 1);
                colored = 1;
                continue;
            }

            if (isdigit((unsigned char)ch)) {
                if (colored) { ab_str(ab, HL_RESET); colored = 0; }
                ab_str(ab, HL_NUMBER);
                while (i < len && (isalnum((unsigned char)data[i])
                                   || data[i] == '.'))
                    ab_append(ab, data + i++, 1);
                ab_str(ab, HL_RESET);
                colored = 0;
                continue;
            }

            if (isalpha((unsigned char)ch) || ch == '_') {
                int kl = 0;
                if (kw_match(data, i, len, py_keywords, &kl)) {
                    if (colored) { ab_str(ab, HL_RESET); colored = 0; }
                    ab_str(ab, HL_KW);
                    ab_append(ab, data + i, kl);
                    ab_str(ab, HL_RESET);
                    i += kl;
                    colored = 0;
                    continue;
                }
            }

            if (colored) { ab_str(ab, HL_RESET); colored = 0; }
            ab_append(ab, data + i++, 1);
            continue;
        }

        if (in_str) {
            if (data[i] == '\\' && i + 1 < len) {
                ab_append(ab, data + i, 2);
                i += 2;
                continue;
            }
            ab_append(ab, data + i, 1);
            if (data[i] == delim) {
                ab_str(ab, HL_RESET);
                in_str  = 0;
                colored = 0;
            }
            i++;
        }
    }

    if (colored) ab_str(ab, HL_RESET);
}

void highlight_line(Abuf *ab, const char *data, int len, int row)
{
    (void)row;
    if (!E.opt_syntax || !E.filetype[0]) {
        ab_append(ab, data, len);
        return;
    }

    const char *ft = E.filetype;
    if (!strcmp(ft, "c") || !strcmp(ft, "h"))
        emit_c(ab, data, len, 0);
    else if (!strcmp(ft, "cpp") || !strcmp(ft, "cc") || !strcmp(ft, "hpp"))
        emit_c(ab, data, len, 1);
    else if (!strcmp(ft, "sh") || !strcmp(ft, "bash") || !strcmp(ft, "make"))
        emit_sh(ab, data, len);
    else if (!strcmp(ft, "py"))
        emit_py(ab, data, len);
    else
        ab_append(ab, data, len);
}
