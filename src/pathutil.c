#if !defined(_WIN32) && !defined(__APPLE__)
    #define _DEFAULT_SOURCE
    #define _POSIX_C_SOURCE 200809L
#endif
#include "pathutil.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
    #include <windows.h>
    #include <direct.h>
#else
    #include <unistd.h>
    #include <limits.h>
    #include <sys/stat.h>
    #include <sys/types.h>
    #include <pwd.h>
    #ifdef __APPLE__
        #include <mach-o/dyld.h>
    #endif
#endif

char path_sep(void) {
#ifdef _WIN32
    return '\\';
#else
    return '/';
#endif
}

int get_executable_path(char *buf, size_t buflen) {
#ifdef _WIN32
    DWORD len = GetModuleFileNameA(NULL, buf, (DWORD)buflen);
    if (len == 0 || len >= buflen) return -1;
    return 0;
#elif defined(__APPLE__)
    uint32_t size = (uint32_t)buflen;
    char tmp[4096];
    uint32_t tsize = sizeof(tmp);
    if (_NSGetExecutablePath(tmp, &tsize) != 0) return -1;
    if (!realpath(tmp, buf)) {
        /* fall back to the unresolved path */
        strncpy(buf, tmp, buflen - 1);
        buf[buflen - 1] = '\0';
    }
    (void)size;
    return 0;
#else
    /* Linux and most other unices expose /proc/self/exe */
    ssize_t len = readlink("/proc/self/exe", buf, buflen - 1);
    if (len >= 0) {
        buf[len] = '\0';
        return 0;
    }
    /* Fallback: /proc/curproc/file (BSD) */
    len = readlink("/proc/curproc/file", buf, buflen - 1);
    if (len >= 0) {
        buf[len] = '\0';
        return 0;
    }
    return -1;
#endif
}

void get_dir_from_path(const char *path, char *dirbuf, size_t buflen) {
    const char *slash = strrchr(path, '/');
#ifdef _WIN32
    const char *bslash = strrchr(path, '\\');
    if (bslash && (!slash || bslash > slash)) slash = bslash;
#endif
    if (!slash) {
        if (buflen > 1) { dirbuf[0] = '.'; dirbuf[1] = '\0'; }
        return;
    }
    size_t n = (size_t)(slash - path);
    if (n == 0) n = 1; /* root */
    if (n >= buflen) n = buflen - 1;
    memcpy(dirbuf, path, n);
    dirbuf[n] = '\0';
}

void path_join(const char *dir, const char *name, char *out, size_t buflen) {
    size_t dlen = strlen(dir);
    char sep = path_sep();
    if (dlen > 0 && dir[dlen - 1] == sep) {
        snprintf(out, buflen, "%s%s", dir, name);
    } else {
        snprintf(out, buflen, "%s%c%s", dir, sep, name);
    }
}

int mkdir_p(const char *path) {
    char tmp[1024];
    size_t len = strlen(path);
    if (len == 0 || len >= sizeof(tmp)) return -1;
    strcpy(tmp, path);

    for (size_t i = 1; i < len; i++) {
        if (tmp[i] == '/' || tmp[i] == '\\') {
            char c = tmp[i];
            tmp[i] = '\0';
#ifdef _WIN32
            _mkdir(tmp);
#else
            mkdir(tmp, 0755);
#endif
            tmp[i] = c;
        }
    }
#ifdef _WIN32
    if (_mkdir(tmp) != 0) {
        /* ignore "already exists" */
    }
#else
    if (mkdir(tmp, 0755) != 0) {
        /* ignore "already exists" */
    }
#endif
    return 0;
}

int get_home_dir(char *buf, size_t buflen) {
#ifdef _WIN32
    const char *up = getenv("USERPROFILE");
    if (up) {
        snprintf(buf, buflen, "%s", up);
        return 0;
    }
    const char *hd = getenv("HOMEDRIVE");
    const char *hp = getenv("HOMEPATH");
    if (hd && hp) {
        snprintf(buf, buflen, "%s%s", hd, hp);
        return 0;
    }
    return -1;
#else
    const char *home = getenv("HOME");
    if (home) {
        snprintf(buf, buflen, "%s", home);
        return 0;
    }
    struct passwd *pw = getpwuid(getuid());
    if (pw && pw->pw_dir) {
        snprintf(buf, buflen, "%s", pw->pw_dir);
        return 0;
    }
    return -1;
#endif
}
