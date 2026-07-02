/*
 * lc - Lazy Command
 * A tiny command-line alias expander with positional-argument templating.

 * Copyright (C) XJTU-WXY. Licensed under the GNU GPLv3.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "strbuf.h"
#include "pathutil.h"
#include "config.h"
#include "template.h"
#include "init.h"

static void print_banner_and_list(const AliasList *list) {
    printf("Lazy Command by XJTU-WXY under GNU GPLv3\n\n");
    if (list->count == 0) {
        printf("No aliases are currently configured. Edit lc_config.txt next to the executable.\n\n");
        printf( "[Format] alias: command template\n"
                "  %%{N}    the N-th positional argument (1-based), can be reused\n"
                "  %%{**}   collects all remaining args starting right after the highest\n"
                "          defined positional index (joined with spaces)\n"
                "  \\%%      escaped literal %%\n"
                "  \\\\      escaped literal \\\n"
                "Lines starting with # or // are comments; blank lines are ignored.\n"
                "Note: \"init\" is a reserved command name and must not appear as an alias here.");
        return;
    }
    printf("Configured aliases:\n");
    /* Align the ':' column for readability. */
    int maxw = 0;
    for (int i = 0; i < list->count; i++) {
        int l = (int)strlen(list->items[i].name);
        if (l > maxw) maxw = l;
    }
    for (int i = 0; i < list->count; i++) {
        printf("  %-*s  -->  %s\n", maxw, list->items[i].name, list->items[i].tmpl);
    }
}

int main(int argc, char **argv) {
    char exe_path[2048];
    if (get_executable_path(exe_path, sizeof(exe_path)) != 0) {
        /* Not fatal for most operations, but lc init needs it. Fall back
         * to argv[0]; it may be a relative path, which is still usually
         * enough since shells resolve it against PATH/cwd at call time. */
        snprintf(exe_path, sizeof(exe_path), "%s", argv[0]);
    }

    /* `lc init ...` is handled before touching the config file at all, so
     * a broken config never blocks the user from (re)initializing. */
    if (argc >= 2 && strcmp(argv[1], "init") == 0) {
        return cmd_init(argc - 2, argv + 2, exe_path);
    }

    char exe_dir[2048];
    get_dir_from_path(exe_path, exe_dir, sizeof(exe_dir));
    char config_path[2200];
    path_join(exe_dir, "lc_config.txt", config_path, sizeof(config_path));

    AliasList list;
    char *load_err = NULL;
    if (config_load(config_path, &list, &load_err) != 0) {
        fprintf(stderr, "lc: failed to load config file: %s\n", load_err);
        free(load_err);
        return 1;
    }

    int ret = 0;
    if (argc == 1) {
        print_banner_and_list(&list);
    } else {
        const char *alias_name = argv[1];
        const Alias *alias = config_find(&list, alias_name);
        if (!alias) {
            fprintf(stderr, "lc: unknown command '%s'\n", alias_name);
            fprintf(stderr, "Run `lc` with no arguments to see all available aliases.\n");
            ret = 1;
        } else {
            char **user_args = argv + 2;
            int user_argc = argc - 2;
            char *err = NULL;
            char *cmd = build_command(alias_name, alias->tmpl, user_args, user_argc, &err);
            if (!cmd) {
                fprintf(stderr, "lc: %s\n", err);
                free(err);
                ret = 1;
            } else {
                printf("%s\n", cmd);
                free(cmd);
            }
        }
    }

    alias_list_free(&list);
    return ret;
}
