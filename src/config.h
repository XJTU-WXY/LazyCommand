#ifndef LC_CONFIG_H
#define LC_CONFIG_H

typedef struct {
    char *name;
    char *tmpl;
} Alias;

typedef struct {
    Alias *items;
    int count;
    int capacity;
} AliasList;

void alias_list_init(AliasList *list);
void alias_list_free(AliasList *list);

int config_load(const char *path, AliasList *list, char **err_out);

/* Look up an alias by name. Returns NULL if not found. */
const Alias *config_find(const AliasList *list, const char *name);

#endif
