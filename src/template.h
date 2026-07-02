#ifndef LC_TEMPLATE_H
#define LC_TEMPLATE_H

/* Build the final command string from an alias template and the user-
 * supplied positional arguments.
 *
 * On success returns a malloc'd string (caller must free) and *err_out
 * is set to NULL.
 * On failure returns NULL and *err_out is set to a malloc'd error message
 * (caller must free).
 *
 * alias_name   : name of the alias, used only for error messages
 * tmpl         : the raw template string, e.g. "conda create -n %{1} python=%{2}"
 * user_args    : array of user-supplied argument strings (argv-style)
 * user_argc    : number of elements in user_args
 */
char *build_command(const char *alias_name, const char *tmpl,
                     char **user_args, int user_argc, char **err_out);

#endif
