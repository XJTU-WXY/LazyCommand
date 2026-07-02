#ifndef LC_INIT_H
#define LC_INIT_H

/* Handle `lc init [shell]`.
 * argv/argc: extra arguments after "init" (e.g. an explicit shell name, or
 * "all"). exe_path: absolute path of the running lc executable, used so the
 * generated shell wrapper always calls the real binary regardless of PATH.
 * Returns a process exit code. */
int cmd_init(int argc, char **argv, const char *exe_path);

#endif
