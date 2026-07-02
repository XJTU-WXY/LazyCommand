#include "strbuf.h"
#include <stdlib.h>
#include <string.h>

static void sb_ensure(StrBuf *sb, size_t extra) {
    if (sb->len + extra + 1 <= sb->cap) return;
    size_t newcap = sb->cap ? sb->cap : 64;
    while (newcap < sb->len + extra + 1) newcap *= 2;
    sb->data = (char *)realloc(sb->data, newcap);
    sb->cap = newcap;
}

void sb_init(StrBuf *sb) {
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
    sb_ensure(sb, 1);
    sb->data[0] = '\0';
}

void sb_free(StrBuf *sb) {
    free(sb->data);
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
}

void sb_append_n(StrBuf *sb, const char *s, size_t n) {
    if (n == 0) return;
    sb_ensure(sb, n);
    memcpy(sb->data + sb->len, s, n);
    sb->len += n;
    sb->data[sb->len] = '\0';
}

void sb_append(StrBuf *sb, const char *s) {
    sb_append_n(sb, s, strlen(s));
}

void sb_append_char(StrBuf *sb, char c) {
    sb_ensure(sb, 1);
    sb->data[sb->len++] = c;
    sb->data[sb->len] = '\0';
}

char *sb_detach(StrBuf *sb) {
    char *out = sb->data;
    if (!out) {
        out = (char *)malloc(1);
        out[0] = '\0';
    }
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
    return out;
}
