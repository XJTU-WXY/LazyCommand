#if !defined(_WIN32) && !defined(__APPLE__)
    #define _DEFAULT_SOURCE
    #define _POSIX_C_SOURCE 200809L
#endif
#include "init.h"
#include "pathutil.h"
#include "strbuf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef _WIN32
    #include <windows.h>
    #include <tlhelp32.h>
#else
    #include <unistd.h>
    #ifdef __APPLE__
        #include <libproc.h>
    #endif
#endif

typedef enum {
    SHELL_UNKNOWN = 0,
    SHELL_BASH,
    SHELL_ZSH,
    SHELL_FISH,
    SHELL_POWERSHELL,
    SHELL_CMD
} ShellType;

static const char *shell_type_name(ShellType t) {
    switch (t) {
        case SHELL_BASH: return "bash";
        case SHELL_ZSH: return "zsh";
        case SHELL_FISH: return "fish";
        case SHELL_POWERSHELL: return "powershell";
        case SHELL_CMD: return "cmd";
        default: return "unknown";
    }
}

static void to_lower_inplace(char *s) {
    for (; *s; s++) *s = (char)tolower((unsigned char)*s);
}

/* Classify a process/base name (e.g. "bash", "zsh.exe", "pwsh") into a
 * ShellType. */
static ShellType classify_name(const char *raw_name) {
    char name[256];
    snprintf(name, sizeof(name), "%s", raw_name);
    to_lower_inplace(name);
    /* strip a trailing ".exe" if present */
    size_t l = strlen(name);
    if (l > 4 && strcmp(name + l - 4, ".exe") == 0) name[l - 4] = '\0';

    if (strcmp(name, "bash") == 0) return SHELL_BASH;
    if (strcmp(name, "sh") == 0 || strcmp(name, "dash") == 0) return SHELL_BASH;
    if (strcmp(name, "zsh") == 0) return SHELL_ZSH;
    if (strcmp(name, "fish") == 0) return SHELL_FISH;
    if (strcmp(name, "powershell") == 0 || strcmp(name, "pwsh") == 0) return SHELL_POWERSHELL;
    if (strcmp(name, "cmd") == 0) return SHELL_CMD;
    return SHELL_UNKNOWN;
}

static ShellType parse_shell_arg(const char *s) {
    char tmp[64];
    snprintf(tmp, sizeof(tmp), "%s", s);
    to_lower_inplace(tmp);
    if (strcmp(tmp, "bash") == 0) return SHELL_BASH;
    if (strcmp(tmp, "zsh") == 0) return SHELL_ZSH;
    if (strcmp(tmp, "fish") == 0) return SHELL_FISH;
    if (strcmp(tmp, "powershell") == 0 || strcmp(tmp, "pwsh") == 0) return SHELL_POWERSHELL;
    if (strcmp(tmp, "cmd") == 0) return SHELL_CMD;
    return SHELL_UNKNOWN;
}

/* ---------------------------------------------------------------------
 * Current-shell detection: inspect the parent process, the same trick
 * `conda init` relies on.
 * --------------------------------------------------------------------- */
#ifdef _WIN32
static ShellType detect_current_shell(void) {
    DWORD pid = GetCurrentProcessId();
    DWORD parent_pid = 0;

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return SHELL_UNKNOWN;

    PROCESSENTRY32 pe;
    pe.dwSize = sizeof(PROCESSENTRY32);
    if (Process32First(snap, &pe)) {
        do {
            if (pe.th32ProcessID == pid) {
                parent_pid = pe.th32ParentProcessID;
                break;
            }
        } while (Process32Next(snap, &pe));
    }

    ShellType result = SHELL_UNKNOWN;
    if (parent_pid != 0) {
        pe.dwSize = sizeof(PROCESSENTRY32);
        if (Process32First(snap, &pe)) {
            do {
                if (pe.th32ProcessID == parent_pid) {
                    result = classify_name(pe.szExeFile);
                    break;
                }
            } while (Process32Next(snap, &pe));
        }
    }
    CloseHandle(snap);

    if (result == SHELL_UNKNOWN) {
        /* Fallback heuristic: presence of PSModulePath strongly implies
         * PowerShell is hosting us somewhere in the ancestry. */
        if (getenv("PSModulePath")) return SHELL_POWERSHELL;
        return SHELL_CMD;
    }
    return result;
}
#else
static ShellType detect_current_shell(void) {
#ifdef __linux__
    pid_t ppid = getppid();
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/comm", (int)ppid);
    FILE *f = fopen(path, "r");
    if (f) {
        char name[256];
        if (fgets(name, sizeof(name), f)) {
            size_t l = strlen(name);
            while (l > 0 && (name[l - 1] == '\n' || name[l - 1] == '\r')) name[--l] = '\0';
            fclose(f);
            ShellType t = classify_name(name);
            if (t != SHELL_UNKNOWN) return t;
        } else {
            fclose(f);
        }
    }
#elif defined(__APPLE__)
    pid_t ppid = getppid();
    char pathbuf[4096];
    if (proc_pidpath(ppid, pathbuf, sizeof(pathbuf)) > 0) {
        const char *base = strrchr(pathbuf, '/');
        base = base ? base + 1 : pathbuf;
        ShellType t = classify_name(base);
        if (t != SHELL_UNKNOWN) return t;
    }
#endif
    /* Fallback: the login shell recorded in $SHELL. Not perfectly accurate
     * (it won't catch e.g. a zsh user who launched bash manually) but a
     * reasonable default when process introspection is unavailable. */
    const char *shell = getenv("SHELL");
    if (shell) {
        const char *base = strrchr(shell, '/');
        base = base ? base + 1 : shell;
        ShellType t = classify_name(base);
        if (t != SHELL_UNKNOWN) return t;
    }
    return SHELL_UNKNOWN;
}
#endif

/* ---------------------------------------------------------------------
 * Generic "marker block" injection into a text config file. Lets `lc init`
 * be re-run safely (idempotent update instead of piling up duplicates).
 * --------------------------------------------------------------------- */
static char *read_whole_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return strdup("");
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return strdup(""); }
    char *buf = (char *)malloc((size_t)sz + 1);
    size_t rd = fread(buf, 1, (size_t)sz, f);
    buf[rd] = '\0';
    fclose(f);
    return buf;
}

static int write_marker_block(const char *filepath, const char *begin_marker,
                               const char *end_marker, const char *body) {
    char dir[1024];
    get_dir_from_path(filepath, dir, sizeof(dir));
    mkdir_p(dir);

    char *existing = read_whole_file(filepath);
    char *begin_pos = strstr(existing, begin_marker);
    char *end_pos = begin_pos ? strstr(begin_pos, end_marker) : NULL;

    StrBuf out;
    sb_init(&out);

    if (begin_pos && end_pos) {
        /* Replace the existing block in place. */
        sb_append_n(&out, existing, (size_t)(begin_pos - existing));
        sb_append(&out, begin_marker);
        sb_append_char(&out, '\n');
        sb_append(&out, body);
        sb_append(&out, end_marker);
        char *after = end_pos + strlen(end_marker);
        sb_append(&out, after);
    } else {
        /* Append a new block at the end of the file. */
        sb_append(&out, existing);
        size_t elen = strlen(existing);
        if (elen > 0 && existing[elen - 1] != '\n') sb_append_char(&out, '\n');
        if (elen > 0) sb_append_char(&out, '\n');
        sb_append(&out, begin_marker);
        sb_append_char(&out, '\n');
        sb_append(&out, body);
        sb_append(&out, end_marker);
        sb_append_char(&out, '\n');
    }
    free(existing);

    FILE *f = fopen(filepath, "wb");
    if (!f) { sb_free(&out); return -1; }
    fwrite(out.data, 1, out.len, f);
    fclose(f);
    sb_free(&out);
    return 0;
}

#define BEGIN_MARKER "# >>> lc initialize >>>"
#define END_MARKER   "# <<< lc initialize <<<"

/* ---------------------------------------------------------------------
 * Per-shell wrapper bodies. Each defines an `lc` function that calls the
 * real binary (by absolute path, so it never recurses into itself) and
 * `eval`s the resulting command string -- this is how we work around the
 * fact that a plain child process cannot change its parent shell's
 * environment (e.g. `conda activate`, `cd`).
 * --------------------------------------------------------------------- */
static char *make_body_bash_zsh(const char *exe_path) {
    StrBuf sb; sb_init(&sb);
    sb_append(&sb, "# !! Contents within this block are managed by 'lc init' !!\n");
    sb_append(&sb, "lc() {\n");
    sb_append(&sb, "    if [ \"$#\" -eq 0 ]; then\n");
    sb_append(&sb, "        \""); sb_append(&sb, exe_path); sb_append(&sb, "\"\n");
    sb_append(&sb, "        return $?\n");
    sb_append(&sb, "    fi\n");
    sb_append(&sb, "    if [ \"$1\" = \"init\" ]; then\n");
    sb_append(&sb, "        \""); sb_append(&sb, exe_path); sb_append(&sb, "\" \"$@\"\n");
    sb_append(&sb, "        return $?\n");
    sb_append(&sb, "    fi\n");
    sb_append(&sb, "    local __lc_out __lc_ret\n");
    sb_append(&sb, "    __lc_out=\"$(\""); sb_append(&sb, exe_path); sb_append(&sb, "\" \"$@\")\"\n");
    sb_append(&sb, "    __lc_ret=$?\n");
    sb_append(&sb, "    if [ $__lc_ret -eq 0 ]; then\n");
    sb_append(&sb, "        eval \"$__lc_out\"\n");
    sb_append(&sb, "    else\n");
    sb_append(&sb, "        [ -n \"$__lc_out\" ] && printf '%s\\n' \"$__lc_out\"\n");
    sb_append(&sb, "        return $__lc_ret\n");
    sb_append(&sb, "    fi\n");
    sb_append(&sb, "}\n");
    return sb_detach(&sb);
}

static char *make_body_fish(const char *exe_path) {
    StrBuf sb; sb_init(&sb);
    sb_append(&sb, "# !! Contents within this block are managed by 'lc init' !!\n");
    sb_append(&sb, "function lc\n");
    sb_append(&sb, "    if test (count $argv) -eq 0\n");
    sb_append(&sb, "        \""); sb_append(&sb, exe_path); sb_append(&sb, "\"\n");
    sb_append(&sb, "        return $status\n");
    sb_append(&sb, "    end\n");
    sb_append(&sb, "    if test \"$argv[1]\" = \"init\"\n");
    sb_append(&sb, "        \""); sb_append(&sb, exe_path); sb_append(&sb, "\" $argv\n");
    sb_append(&sb, "        return $status\n");
    sb_append(&sb, "    end\n");
    sb_append(&sb, "    set -l __lc_out (\""); sb_append(&sb, exe_path); sb_append(&sb, "\" $argv)\n");
    sb_append(&sb, "    set -l __lc_ret $status\n");
    sb_append(&sb, "    if test $__lc_ret -eq 0\n");
    sb_append(&sb, "        eval $__lc_out\n");
    sb_append(&sb, "    else\n");
    sb_append(&sb, "        if test -n \"$__lc_out\"\n");
    sb_append(&sb, "            echo $__lc_out\n");
    sb_append(&sb, "        end\n");
    sb_append(&sb, "        return $__lc_ret\n");
    sb_append(&sb, "    end\n");
    sb_append(&sb, "end\n");
    return sb_detach(&sb);
}

static char *make_body_powershell(const char *exe_path) {
    StrBuf sb; sb_init(&sb);
    sb_append(&sb, "# !! Contents within this block are managed by 'lc init' !!\n");
    sb_append(&sb, "function lc {\n");
    sb_append(&sb, "    if ($args.Count -eq 0) {\n");
    sb_append(&sb, "        & \""); sb_append(&sb, exe_path); sb_append(&sb, "\"\n");
    sb_append(&sb, "        return\n");
    sb_append(&sb, "    }\n");
    sb_append(&sb, "    if ($args[0] -eq \"init\") {\n");
    sb_append(&sb, "        & \""); sb_append(&sb, exe_path); sb_append(&sb, "\" @args\n");
    sb_append(&sb, "        return\n");
    sb_append(&sb, "    }\n");
    sb_append(&sb, "    $__lc_out = & \""); sb_append(&sb, exe_path); sb_append(&sb, "\" @args\n");
    sb_append(&sb, "    if ($LASTEXITCODE -eq 0) {\n");
    sb_append(&sb, "        Invoke-Expression (($__lc_out) -join \"`n\")\n");
    sb_append(&sb, "    } else {\n");
    sb_append(&sb, "        if ($__lc_out) { Write-Output $__lc_out }\n");
    sb_append(&sb, "        return $LASTEXITCODE\n");
    sb_append(&sb, "    }\n");
    sb_append(&sb, "}\n");
    return sb_detach(&sb);
}

/* ---------------------------------------------------------------------
 * Per-shell rc file locations and top-level init dispatch.
 * --------------------------------------------------------------------- */
static int init_posix_rc(ShellType shell, const char *exe_path) {
    char home[1024];
    if (get_home_dir(home, sizeof(home)) != 0) {
        fprintf(stderr, "lc: could not determine the user's home directory\n");
        return 1;
    }
    char rcfile[1200];
    char *body = NULL;

    if (shell == SHELL_BASH) {
        path_join(home, ".bashrc", rcfile, sizeof(rcfile));
        body = make_body_bash_zsh(exe_path);
    } else if (shell == SHELL_ZSH) {
        path_join(home, ".zshrc", rcfile, sizeof(rcfile));
        body = make_body_bash_zsh(exe_path);
    } else if (shell == SHELL_FISH) {
        char fishdir[1100];
        path_join(home, ".config/fish", fishdir, sizeof(fishdir));
        mkdir_p(fishdir);
        path_join(fishdir, "config.fish", rcfile, sizeof(rcfile));
        body = make_body_fish(exe_path);
    } else if (shell == SHELL_POWERSHELL) {
        /* PowerShell (Core) on Linux/macOS: ~/.config/powershell/profile.ps1 */
        char psdir[1100];
        path_join(home, ".config/powershell", psdir, sizeof(psdir));
        mkdir_p(psdir);
        path_join(psdir, "profile.ps1", rcfile, sizeof(rcfile));
        body = make_body_powershell(exe_path);
    } else {
        fprintf(stderr, "lc: shell type '%s' is not supported on this platform\n", shell_type_name(shell));
        return 1;
    }

    int rc = write_marker_block(rcfile, BEGIN_MARKER, END_MARKER, body);
    free(body);
    if (rc != 0) {
        fprintf(stderr, "lc: failed to write config file: %s\n", rcfile);
        return 1;
    }
    printf("Initialized %s: %s\n", shell_type_name(shell), rcfile);
    printf("Open a new terminal, or run `source \"%s\"`, for it to take effect.\n", rcfile);
    return 0;
}

#ifdef _WIN32
static int init_windows_powershell(const char *exe_path) {
    char home[1024];
    if (get_home_dir(home, sizeof(home)) != 0) {
        fprintf(stderr, "lc: could not determine the user's home directory\n");
        return 1;
    }
    /* PowerShell 7+ (pwsh) profile location on Windows. Also fine for
     * Windows PowerShell 5.1 users since PowerShell reads whichever
     * profile.ps1 matches its own $PROFILE, but 5.1's default folder is
     * "WindowsPowerShell" instead of "PowerShell" -- write to both so the
     * wrapper is picked up regardless of which one launched lc. */
    const char *subdirs[2] = { "Documents\\PowerShell", "Documents\\WindowsPowerShell" };
    char *body = make_body_powershell(exe_path);
    int any_ok = 0;
    for (int i = 0; i < 2; i++) {
        char dir[1100], rcfile[1200];
        path_join(home, subdirs[i], dir, sizeof(dir));
        mkdir_p(dir);
        path_join(dir, "profile.ps1", rcfile, sizeof(rcfile));
        if (write_marker_block(rcfile, BEGIN_MARKER, END_MARKER, body) == 0) {
            printf("Initialized PowerShell: %s\n", rcfile);
            any_ok = 1;
        }
    }
    free(body);
    if (!any_ok) {
        fprintf(stderr, "lc: failed to write PowerShell profile\n");
        return 1;
    }
    printf("Open a new PowerShell window for it to take effect.\n");
    return 0;
}

static int init_windows_cmd(const char *exe_path) {
    char home[1024];
    if (get_home_dir(home, sizeof(home)) != 0) {
        fprintf(stderr, "lc: could not determine the user's home directory\n");
        return 1;
    }
    char lcdir[1100];
    path_join(home, "AppData\\Local\\lc", lcdir, sizeof(lcdir));
    mkdir_p(lcdir);

    char wrapper_bat[1200], macro_file[1200];
    path_join(lcdir, "lc_wrapper.bat", wrapper_bat, sizeof(wrapper_bat));
    path_join(lcdir, "lc_macros.txt", macro_file, sizeof(macro_file));

    /* 1) Write the wrapper batch script. */
    FILE *f = fopen(wrapper_bat, "wb");
    if (!f) { fprintf(stderr, "lc: could not write to %s\n", wrapper_bat); return 1; }
    fprintf(f,
        "@echo off\r\n"
        "if \"%%~1\"==\"\" (\r\n"
        "    \"%s\"\r\n"
        "    exit /b %%errorlevel%%\r\n"
        ")\r\n"
        "if /I \"%%~1\"==\"init\" (\r\n"
        "    \"%s\" %%*\r\n"
        "    exit /b %%errorlevel%%\r\n"
        ")\r\n"
        "for /f \"usebackq delims=\" %%%%L in (`\"%s\" %%*`) do (\r\n"
        "    %%%%L\r\n"
        ")\r\n",
        exe_path, exe_path, exe_path);
    fclose(f);

    /* 2) Write the doskey macro file (idempotent: single line, overwrite). */
    f = fopen(macro_file, "wb");
    if (!f) { fprintf(stderr, "lc: could not write to %s\n", macro_file); return 1; }
    fprintf(f, "lc=call \"%s\" $*\r\n", wrapper_bat);
    fclose(f);

    /* 3) Hook it into every new cmd.exe session via the AutoRun registry
     *    value, preserving whatever was already there. */
    const char *query_cmd = "reg query \"HKCU\\Software\\Microsoft\\Command Processor\" /v AutoRun 2>nul";
    char cur_value[2048] = "";
    FILE *p = _popen(query_cmd, "r");
    if (p) {
        char line[1024];
        while (fgets(line, sizeof(line), p)) {
            char *pos = strstr(line, "REG_SZ");
            if (pos) {
                pos += 6;
                while (*pos == ' ' || *pos == '\t') pos++;
                size_t l = strlen(pos);
                while (l > 0 && (pos[l - 1] == '\n' || pos[l - 1] == '\r')) pos[--l] = '\0';
                snprintf(cur_value, sizeof(cur_value), "%s", pos);
            }
        }
        _pclose(p);
    }

    if (strstr(cur_value, macro_file) != NULL) {
        printf("Initialized cmd (AutoRun already contains the lc macro, nothing to change).\n");
    } else {
        char new_value[3072];
        if (cur_value[0] != '\0') {
            snprintf(new_value, sizeof(new_value), "%s & doskey /macrofile=\"%s\"", cur_value, macro_file);
        } else {
            snprintf(new_value, sizeof(new_value), "doskey /macrofile=\"%s\"", macro_file);
        }
        char add_cmd[3200];
        snprintf(add_cmd, sizeof(add_cmd),
                 "reg add \"HKCU\\Software\\Microsoft\\Command Processor\" /v AutoRun /d \"%s\" /f >nul",
                 new_value);
        int rc = system(add_cmd);
        if (rc != 0) {
            fprintf(stderr, "lc: failed to write the AutoRun registry value; please retry, or run this manually:\n  %s\n", add_cmd);
            return 1;
        }
        printf("Initialized cmd, wrapper script: %s\n", wrapper_bat);
    }
    printf("Open a new cmd window for it to take effect.\n");
    return 0;
}
#endif

static int init_one_shell(ShellType shell, const char *exe_path) {
    if (shell == SHELL_UNKNOWN) return 1;
#ifdef _WIN32
    if (shell == SHELL_POWERSHELL) return init_windows_powershell(exe_path);
    if (shell == SHELL_CMD) return init_windows_cmd(exe_path);
    if (shell == SHELL_BASH || shell == SHELL_ZSH || shell == SHELL_FISH) {
        /* e.g. Git-Bash / MSYS / WSL-adjacent bash running natively on
         * Windows still uses a POSIX-style rc file under %USERPROFILE%. */
        return init_posix_rc(shell, exe_path);
    }
    return 1;
#else
    return init_posix_rc(shell, exe_path);
#endif
}

int cmd_init(int argc, char **argv, const char *exe_path) {
    if (argc >= 1 && parse_shell_arg(argv[0]) == SHELL_UNKNOWN &&
        strcmp(argv[0], "all") != 0) {
        fprintf(stderr, "lc: unknown shell type '%s'\n", argv[0]);
        fprintf(stderr, "Supported types: bash zsh fish powershell cmd all\n");
        return 1;
    }

    if (argc >= 1 && strcmp(argv[0], "all") == 0) {
        ShellType all[] = {
#ifdef _WIN32
            SHELL_POWERSHELL, SHELL_CMD, SHELL_BASH
#else
            SHELL_BASH, SHELL_ZSH, SHELL_FISH, SHELL_POWERSHELL
#endif
        };
        int n = (int)(sizeof(all) / sizeof(all[0]));
        int failures = 0;
        for (int i = 0; i < n; i++) {
            if (init_one_shell(all[i], exe_path) != 0) failures++;
        }
        return failures ? 1 : 0;
    }

    ShellType shell;
    if (argc >= 1) {
        shell = parse_shell_arg(argv[0]);
    } else {
        shell = detect_current_shell();
        if (shell == SHELL_UNKNOWN) {
            fprintf(stderr, "lc: could not auto-detect the current terminal type.\n");
            fprintf(stderr, "Please specify it manually, e.g.: lc init bash | zsh | fish | powershell | cmd | all\n");
            return 1;
        }
        printf("Detected current terminal type: %s\n", shell_type_name(shell));
    }

    return init_one_shell(shell, exe_path);
}
