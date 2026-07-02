#ifndef LC_PATHUTIL_H
#define LC_PATHUTIL_H
#include <stddef.h>

/* Get the absolute path of the currently running executable.
 * Returns 0 on success, -1 on failure. buf will be NUL-terminated. */
int get_executable_path(char *buf, size_t buflen);

/* Given a full path, write the directory part (without trailing separator,
 * unless it's a root) into dirbuf. */
void get_dir_from_path(const char *path, char *dirbuf, size_t buflen);

/* Join a directory and a filename with the platform separator into out. */
void path_join(const char *dir, const char *name, char *out, size_t buflen);

/* Recursively create a directory (like `mkdir -p`). Returns 0 on success. */
int mkdir_p(const char *path);

/* Get the current user's home directory. Returns 0 on success. */
int get_home_dir(char *buf, size_t buflen);

/* Platform path separator character, e.g. '/' or '\\'. */
char path_sep(void);

#endif
