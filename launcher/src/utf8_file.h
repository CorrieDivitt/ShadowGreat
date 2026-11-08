/* utf8_file.h - open files by UTF-8 path (Windows paths can hold any language). */
#pragma once
#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif
FILE *utf8_fopen(const char *path, const char *mode);
#ifdef __cplusplus
}
#endif
