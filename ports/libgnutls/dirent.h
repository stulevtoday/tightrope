/*
 * Minimal dirent.h for MSVC — implements POSIX directory iteration using
 * Win32 FindFirstFile/FindNextFile.  Provides wide-char (_WDIR) and TCHAR
 * (_TDIR) variants that libgnutls/verify-high2.c expects on Windows.
 *
 * Based on tronkko/dirent (MIT licence).
 * Adapted for use as a vcpkg port overlay by the tightrope project.
 */
#ifndef DIRENT_H
#define DIRENT_H

#if defined(_WIN32) && !defined(__CYGWIN__)

#include <windows.h>
#include <wchar.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <stddef.h>

#define DT_UNKNOWN  0
#define DT_FIFO     1
#define DT_CHR      2
#define DT_DIR      4
#define DT_BLK      6
#define DT_REG      8
#define DT_LNK     10
#define DT_SOCK    12
#define _DIRENT_HAVE_D_TYPE

/* Wide-character directory entry */
struct _wdirent {
    long            d_ino;
    unsigned short  d_reclen;
    size_t          d_namlen;
    int             d_type;
    wchar_t         d_name[MAX_PATH + 1];
};

/* Wide-character directory stream */
typedef struct _WDIR {
    struct _wdirent ent;
    WIN32_FIND_DATAW data;
    HANDLE          handle;
    wchar_t         patt[MAX_PATH + 4];
    int             cached;
} _WDIR;

/* Narrow-character directory entry */
struct dirent {
    long            d_ino;
    unsigned short  d_reclen;
    size_t          d_namlen;
    int             d_type;
    char            d_name[MAX_PATH + 1];
};

/* Narrow-character directory stream */
typedef struct DIR {
    struct dirent   ent;
    WIN32_FIND_DATAA data;
    HANDLE          handle;
    char            patt[MAX_PATH + 4];
    int             cached;
} DIR;

/* ---- wide-char implementation ---- */

static inline int _wdirent_type(DWORD attr) {
    if (attr & FILE_ATTRIBUTE_REPARSE_POINT) return DT_LNK;
    if (attr & FILE_ATTRIBUTE_DIRECTORY)     return DT_DIR;
    return DT_REG;
}

static inline _WDIR *_wopendir(const wchar_t *name) {
    if (!name || name[0] == L'\0') { errno = ENOENT; return NULL; }
    _WDIR *dir = (_WDIR *)malloc(sizeof(_WDIR));
    if (!dir) { errno = ENOMEM; return NULL; }
    size_t n = wcslen(name);
    if (n + 3 > MAX_PATH + 3) { free(dir); errno = ENAMETOOLONG; return NULL; }
    wcscpy_s(dir->patt, MAX_PATH + 4, name);
    if (name[n-1] != L'/' && name[n-1] != L'\\')
        wcscat_s(dir->patt, MAX_PATH + 4, L"\\");
    wcscat_s(dir->patt, MAX_PATH + 4, L"*");
    dir->handle = FindFirstFileW(dir->patt, &dir->data);
    if (dir->handle == INVALID_HANDLE_VALUE) {
        free(dir);
        errno = ENOENT;
        return NULL;
    }
    dir->cached = 1;
    return dir;
}

static inline struct _wdirent *_wreaddir(_WDIR *dir) {
    if (!dir) { errno = EBADF; return NULL; }
    WIN32_FIND_DATAW fd;
    if (dir->cached) {
        dir->cached = 0;
        fd = dir->data;
    } else {
        if (!FindNextFileW(dir->handle, &fd)) return NULL;
    }
    dir->ent.d_ino    = 0;
    dir->ent.d_reclen = sizeof(struct _wdirent);
    dir->ent.d_namlen = wcslen(fd.cFileName);
    dir->ent.d_type   = _wdirent_type(fd.dwFileAttributes);
    wcscpy_s(dir->ent.d_name, MAX_PATH + 1, fd.cFileName);
    return &dir->ent;
}

static inline int _wclosedir(_WDIR *dir) {
    if (!dir) { errno = EBADF; return -1; }
    FindClose(dir->handle);
    free(dir);
    return 0;
}

/* ---- narrow-char implementation ---- */

static inline int _dirent_type(DWORD attr) {
    if (attr & FILE_ATTRIBUTE_REPARSE_POINT) return DT_LNK;
    if (attr & FILE_ATTRIBUTE_DIRECTORY)     return DT_DIR;
    return DT_REG;
}

static inline DIR *opendir(const char *name) {
    if (!name || name[0] == '\0') { errno = ENOENT; return NULL; }
    DIR *dir = (DIR *)malloc(sizeof(DIR));
    if (!dir) { errno = ENOMEM; return NULL; }
    size_t n = strlen(name);
    if (n + 3 > MAX_PATH + 3) { free(dir); errno = ENAMETOOLONG; return NULL; }
    strcpy_s(dir->patt, MAX_PATH + 4, name);
    if (name[n-1] != '/' && name[n-1] != '\\')
        strcat_s(dir->patt, MAX_PATH + 4, "\\");
    strcat_s(dir->patt, MAX_PATH + 4, "*");
    dir->handle = FindFirstFileA(dir->patt, &dir->data);
    if (dir->handle == INVALID_HANDLE_VALUE) {
        free(dir);
        errno = ENOENT;
        return NULL;
    }
    dir->cached = 1;
    return dir;
}

static inline struct dirent *readdir(DIR *dir) {
    if (!dir) { errno = EBADF; return NULL; }
    WIN32_FIND_DATAA fd;
    if (dir->cached) {
        dir->cached = 0;
        fd = dir->data;
    } else {
        if (!FindNextFileA(dir->handle, &fd)) return NULL;
    }
    dir->ent.d_ino    = 0;
    dir->ent.d_reclen = sizeof(struct dirent);
    dir->ent.d_namlen = strlen(fd.cFileName);
    dir->ent.d_type   = _dirent_type(fd.dwFileAttributes);
    strcpy_s(dir->ent.d_name, MAX_PATH + 1, fd.cFileName);
    return &dir->ent;
}

static inline int closedir(DIR *dir) {
    if (!dir) { errno = EBADF; return -1; }
    FindClose(dir->handle);
    free(dir);
    return 0;
}

/* ---- TCHAR aliases ---- */

#ifdef UNICODE
#define _TDIR       _WDIR
#define _tdirent    _wdirent
#define _topendir   _wopendir
#define _treaddir   _wreaddir
#define _tclosedir  _wclosedir
#else
#define _TDIR       DIR
#define _tdirent    dirent
#define _topendir   opendir
#define _treaddir   readdir
#define _tclosedir  closedir
#endif

#endif /* _WIN32 */
#endif /* DIRENT_H */
