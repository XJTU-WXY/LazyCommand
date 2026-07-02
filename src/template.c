#if !defined(_WIN32) && !defined(__APPLE__)
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "template.h"
#include "strbuf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef enum { TOK_LITERAL, TOK_POS, TOK_VARARGS } TokType;

typedef struct {
    TokType type;
    char *literal; /* only for TOK_LITERAL */
    int index;     /* only for TOK_POS (1-based) */
} Token;

typedef struct {
    Token *items;
    int count;
    int capacity;
} TokenList;

static void tok_init(TokenList *tl) { tl->items = NULL; tl->count = 0; tl->capacity = 0; }

static void tok_push(TokenList *tl, Token t) {
    if (tl->count >= tl->capacity) {
        tl->capacity = tl->capacity ? tl->capacity * 2 : 8;
        tl->items = (Token *)realloc(tl->items, sizeof(Token) * tl->capacity);
    }
    tl->items[tl->count++] = t;
}

static void tok_free(TokenList *tl) {
    for (int i = 0; i < tl->count; i++) {
        if (tl->items[i].type == TOK_LITERAL) free(tl->items[i].literal);
    }
    free(tl->items);
    tl->items = NULL;
    tl->count = 0;
    tl->capacity = 0;
}

static void flush_literal(StrBuf *cur, TokenList *tl) {
    if (cur->len == 0) return;
    Token t;
    t.type = TOK_LITERAL;
    t.literal = sb_detach(cur);
    t.index = 0;
    tok_push(tl, t);
    sb_init(cur);
}

/* Parse `tmpl` into a token list. Returns 0 on success, -1 on error (with
 * *err_out set to a malloc'd message). Fills *max_index and *has_varargs. */
static int tokenize(const char *alias_name, const char *tmpl, TokenList *tl,
                     int *max_index, int *has_varargs, char **err_out) {
    StrBuf cur;
    sb_init(&cur);
    *max_index = 0;
    *has_varargs = 0;

    size_t len = strlen(tmpl);
    size_t i = 0;
    while (i < len) {
        char c = tmpl[i];
        if (c == '\\') {
            char next = (i + 1 < len) ? tmpl[i + 1] : '\0';
            if (next == '%' || next == '\\') {
                sb_append_char(&cur, next);
                i += 2;
            } else {
                sb_append_char(&cur, '\\');
                i += 1;
            }
        } else if (c == '%' && i + 1 < len && tmpl[i + 1] == '{') {
            size_t close = i + 2;
            while (close < len && tmpl[close] != '}') close++;
            if (close >= len) {
                sb_free(&cur);
                tok_free(tl);
                char buf[256];
                snprintf(buf, sizeof(buf), "alias '%s': unclosed '%%{' in template", alias_name);
                *err_out = strdup(buf);
                return -1;
            }
            size_t content_len = close - (i + 2);
            char content[64];
            if (content_len >= sizeof(content)) content_len = sizeof(content) - 1;
            memcpy(content, tmpl + i + 2, content_len);
            content[content_len] = '\0';

            if (strcmp(content, "**") == 0) {
                flush_literal(&cur, tl);
                Token t; t.type = TOK_VARARGS; t.literal = NULL; t.index = 0;
                tok_push(tl, t);
                *has_varargs = 1;
            } else {
                int all_digits = content_len > 0;
                for (size_t k = 0; k < content_len; k++) {
                    if (!isdigit((unsigned char)content[k])) { all_digits = 0; break; }
                }
                if (!all_digits) {
                    sb_free(&cur);
                    tok_free(tl);
                    char buf[256];
                    snprintf(buf, sizeof(buf), "alias '%s': invalid placeholder '%%{%s}' in template", alias_name, content);
                    *err_out = strdup(buf);
                    return -1;
                }
                int idx = atoi(content);
                if (idx < 1) {
                    sb_free(&cur);
                    tok_free(tl);
                    char buf[256];
                    snprintf(buf, sizeof(buf), "alias '%s': placeholder index must start at 1: '%%{%s}'", alias_name, content);
                    *err_out = strdup(buf);
                    return -1;
                }
                flush_literal(&cur, tl);
                Token t; t.type = TOK_POS; t.literal = NULL; t.index = idx;
                tok_push(tl, t);
                if (idx > *max_index) *max_index = idx;
            }
            i = close + 1;
        } else {
            sb_append_char(&cur, c);
            i += 1;
        }
    }
    flush_literal(&cur, tl);
    sb_free(&cur);
    return 0;
}

char *build_command(const char *alias_name, const char *tmpl,
                     char **user_args, int user_argc, char **err_out) {
    *err_out = NULL;
    TokenList tl;
    tok_init(&tl);
    int max_index = 0, has_varargs = 0;
    if (tokenize(alias_name, tmpl, &tl, &max_index, &has_varargs, err_out) != 0) {
        return NULL;
    }

    if (has_varargs) {
        if (user_argc < max_index) {
            char buf[256];
            snprintf(buf, sizeof(buf),
                     "alias '%s' requires at least %d argument(s), but %d were given",
                     alias_name, max_index, user_argc);
            *err_out = strdup(buf);
            tok_free(&tl);
            return NULL;
        }
    } else {
        if (user_argc != max_index) {
            char buf[256];
            snprintf(buf, sizeof(buf),
                     "alias '%s' requires exactly %d argument(s), but %d were given",
                     alias_name, max_index, user_argc);
            *err_out = strdup(buf);
            tok_free(&tl);
            return NULL;
        }
    }

    StrBuf out;
    sb_init(&out);
    for (int i = 0; i < tl.count; i++) {
        Token *t = &tl.items[i];
        if (t->type == TOK_LITERAL) {
            sb_append(&out, t->literal);
        } else if (t->type == TOK_POS) {
            /* index is 1-based and already validated <= user_argc */
            sb_append(&out, user_args[t->index - 1]);
        } else { /* TOK_VARARGS */
            for (int j = max_index; j < user_argc; j++) {
                if (j > max_index) sb_append_char(&out, ' ');
                sb_append(&out, user_args[j]);
            }
        }
    }

    tok_free(&tl);
    return sb_detach(&out);
}
