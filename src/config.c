#if !defined(_WIN32) && !defined(__APPLE__)
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define RESERVED_NAME "init"

void alias_list_init(AliasList *list) {
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

void alias_list_free(AliasList *list) {
    for (int i = 0; i < list->count; i++) {
        free(list->items[i].name);
        free(list->items[i].tmpl);
    }
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

static void alias_list_push(AliasList *list, const char *name, const char *tmpl) {
    /* If the name already exists, overwrite it (last definition wins). */
    for (int i = 0; i < list->count; i++) {
        if (strcmp(list->items[i].name, name) == 0) {
            free(list->items[i].tmpl);
            list->items[i].tmpl = strdup(tmpl);
            fprintf(stderr, "lc: warning: alias '%s' is defined more than once; using the last definition.\n", name);
            return;
        }
    }
    if (list->count >= list->capacity) {
        list->capacity = list->capacity ? list->capacity * 2 : 16;
        list->items = (Alias *)realloc(list->items, sizeof(Alias) * list->capacity);
    }
    list->items[list->count].name = strdup(name);
    list->items[list->count].tmpl = strdup(tmpl);
    list->count++;
}

const Alias *config_find(const AliasList *list, const char *name) {
    for (int i = 0; i < list->count; i++) {
        if (strcmp(list->items[i].name, name) == 0) return &list->items[i];
    }
    return NULL;
}

static char *trim(char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    if (*s == '\0') return s;
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) { *end = '\0'; end--; }
    return s;
}

static int has_whitespace(const char *s) {
    for (const char *p = s; *p; p++) if (isspace((unsigned char)*p)) return 1;
    return 0;
}

int config_load(const char *path, AliasList *list, char **err_out) {
    alias_list_init(list);
    *err_out = NULL;

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        /* No config file yet: not fatal, just an empty alias set. */
        return 0;
    }

    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (fsize < 0) { fclose(fp); return 0; }

    char *content = (char *)malloc((size_t)fsize + 1);
    size_t rd = fread(content, 1, (size_t)fsize, fp);
    content[rd] = '\0';
    fclose(fp);

    int line_no = 0;
    char *saveptr = NULL;
    char *line = strtok_r(content, "\n", &saveptr);
    while (line != NULL) {
        line_no++;
        /* Strip trailing \r for Windows-style line endings */
        size_t l = strlen(line);
        if (l > 0 && line[l - 1] == '\r') line[l - 1] = '\0';

        char *trimmed = trim(line);
        if (trimmed[0] == '\0' || trimmed[0] == '#' ||
            (trimmed[0] == '/' && trimmed[1] == '/')) {
            line = strtok_r(NULL, "\n", &saveptr);
            continue;
        }

        /* Find the first unescaped ':' separating name and template. */
        char *colon = NULL;
        for (char *p = trimmed; *p; p++) {
            if (*p == '\\' && *(p + 1) != '\0') { p++; continue; }
            if (*p == ':') { colon = p; break; }
        }
        if (!colon) {
            free(content);
            char buf[256];
            snprintf(buf, sizeof(buf), "config file line %d: malformed line (missing ':'): %s", line_no, trimmed);
            *err_out = strdup(buf);
            alias_list_free(list);
            return -1;
        }

        *colon = '\0';
        char *name = trim(trimmed);
        char *tmpl = trim(colon + 1);

        if (name[0] == '\0') {
            free(content);
            char buf[256];
            snprintf(buf, sizeof(buf), "config file line %d: malformed line (empty alias name)", line_no);
            *err_out = strdup(buf);
            alias_list_free(list);
            return -1;
        }
        if (has_whitespace(name)) {
            free(content);
            char buf[256];
            snprintf(buf, sizeof(buf), "config file line %d: alias '%s' must not contain whitespace", line_no, name);
            *err_out = strdup(buf);
            alias_list_free(list);
            return -1;
        }
        if (strcmp(name, RESERVED_NAME) == 0) {
            free(content);
            char buf[256];
            snprintf(buf, sizeof(buf),
                     "config file line %d: 'init' is a reserved command name and cannot be used as an alias", line_no);
            *err_out = strdup(buf);
            alias_list_free(list);
            return -1;
        }
        if (tmpl[0] == '\0') {
            free(content);
            char buf[256];
            snprintf(buf, sizeof(buf), "config file line %d: alias '%s' has an empty command template", line_no, name);
            *err_out = strdup(buf);
            alias_list_free(list);
            return -1;
        }

        alias_list_push(list, name, tmpl);
        line = strtok_r(NULL, "\n", &saveptr);
    }

    free(content);
    return 0;
}
