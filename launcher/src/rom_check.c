/* rom_check.c - is this the supported Shadowgate ROM?
 *
 * Supported: Shadowgate (USA), iNES. The check is the SHA-1 of everything
 * after the 16-byte header (headers differ between dumps, the game does
 * not). Only checksums are stored here, never any of the ROM. */
#include "rom_check.h"
#include "sha1.h"
#include "utf8_file.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SUPPORTED_BODY_SHA1 "e7869c683bd93275cde4536cd2ddf37f230dd8d0"
#define SUPPORTED_SIZE      262160L     /* 16-byte header + 256 KiB */

RomCheck rom_check(const char *path)
{
    RomCheck r;
    memset(&r, 0, sizeof r);
    FILE *f = utf8_fopen(path, "rb");
    if (!f) { snprintf(r.message, sizeof r.message, "The file could not be opened."); return r; }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 16 || size > 8L * 1024 * 1024) {
        fclose(f);
        snprintf(r.message, sizeof r.message, "This doesn't look like an NES ROM (wrong size).");
        return r;
    }
    unsigned char *data = (unsigned char *)malloc((size_t)size);
    size_t got = data ? fread(data, 1, (size_t)size, f) : 0;
    fclose(f);
    if (got != (size_t)size) {
        free(data);
        snprintf(r.message, sizeof r.message, "The file could not be read.");
        return r;
    }
    if (memcmp(data, "NES\x1A", 4) != 0) {
        free(data);
        snprintf(r.message, sizeof r.message, "This isn't an iNES (.nes) file.");
        return r;
    }
    Sha1 s;
    uint8_t d[20];
    sha1_init(&s);
    sha1_update(&s, data + 16, (size_t)size - 16);
    sha1_final(&s, d);
    sha1_hex(d, r.sha1_body);
    free(data);
    if (strcmp(r.sha1_body, SUPPORTED_BODY_SHA1) == 0) {
        r.ok = true;
        snprintf(r.message, sizeof r.message, "Verified: Shadowgate (USA).");
    } else if (size != SUPPORTED_SIZE) {
        snprintf(r.message, sizeof r.message,
                 "This NES ROM isn't Shadowgate (USA) (it's %ld bytes; that version is %ld).", size, SUPPORTED_SIZE);
    } else {
        snprintf(r.message, sizeof r.message,
                 "This isn't the supported version. Only Shadowgate (USA) is supported.");
    }
    return r;
}
