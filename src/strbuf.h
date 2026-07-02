#ifndef LC_STRBUF_H
#define LC_STRBUF_H
#include <stddef.h>

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} StrBuf;

void sb_init(StrBuf *sb);
void sb_free(StrBuf *sb);
void sb_append(StrBuf *sb, const char *s);
void sb_append_n(StrBuf *sb, const char *s, size_t n);
void sb_append_char(StrBuf *sb, char c);
/* Detach the internal buffer (caller owns the returned pointer, must free()).
 * The StrBuf is reset to an empty state afterwards. */
char *sb_detach(StrBuf *sb);

#endif
