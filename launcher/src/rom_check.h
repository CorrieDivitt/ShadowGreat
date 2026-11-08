/* rom_check.h - is this the supported Shadowgate ROM? */
#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    bool ok;
    char message[256];   /* what to tell the player */
    char sha1_body[41];  /* SHA-1 without the 16-byte iNES header ("" if not read) */
} RomCheck;

/* path is UTF-8. */
RomCheck rom_check(const char *path);
#ifdef __cplusplus
}
#endif
