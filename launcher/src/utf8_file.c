/* utf8_file.c - open files by UTF-8 path. */
#include "utf8_file.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wchar.h>
FILE *utf8_fopen(const char *path, const char *mode)
{
    wchar_t wp[1024], wm[16];
    if (!MultiByteToWideChar(CP_UTF8, 0, path, -1, wp, 1024)) return NULL;
    if (!MultiByteToWideChar(CP_UTF8, 0, mode, -1, wm, 16)) return NULL;
    return _wfopen(wp, wm);
}
#else
FILE *utf8_fopen(const char *path, const char *mode) { return fopen(path, mode); }
#endif
