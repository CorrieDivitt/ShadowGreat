/* rom_check.c - is this the supported Shadowgate ROM?
 *
 * Supported: USA, Europe, Sweden (SCN), France, Germany (iNES).
 * The check is the SHA-1 of everything after the 16-byte header
 * (headers differ between dumps, the game does not).
 * Only checksums are stored here, never any of the ROM. */
#include "rom_check.h"
#include "sha1.h"
#include "utf8_file.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SUPPORTED_SIZE      262160L     /* 16-byte header + 256 KiB */

struct RomEntry {
    const char *sha1;
    const char *name;
};

static const struct RomEntry SUPPORTED_ROMS[] = {
    { "e7869c683bd93275cde4536cd2ddf37f230dd8d0", "Shadowgate (USA)" },
    { "0efc8ba8a306c423b32853385274208f42626e2e", "Shadowgate (Europe)" },
    { "757ab53fbff0adf1c2c2e05a35ab861365021057", "Shadowgate (Sweden)" },
    { "149eca5642642bed5fd98b12d682235c02def342", "Shadowgate (France)" },
    { "08625db56ba9a2a66e636590c51044c6726c5b26", "Shadowgate (Germany)" },
};

#define JAPAN_BODY_SHA1 "2f03cecd09059b8aab0ca9553893c29f8ac1c1f5"

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

    for (size_t i = 0; i < sizeof(SUPPORTED_ROMS) / sizeof(SUPPORTED_ROMS[0]); ++i) {
        if (strcmp(r.sha1_body, SUPPORTED_ROMS[i].sha1) == 0) {
            r.ok = true;
            snprintf(r.region_name, sizeof r.region_name, "%s", SUPPORTED_ROMS[i].name);
            snprintf(r.message, sizeof r.message, "Verified: %s.", SUPPORTED_ROMS[i].name);
            return r;
        }
    }

    if (strcmp(r.sha1_body, JAPAN_BODY_SHA1) == 0) {
        snprintf(r.message, sizeof r.message,
                 "Shadowgate (Japan) detected, but Japanese script is not yet supported.");
    } else if (size != SUPPORTED_SIZE) {
        snprintf(r.message, sizeof r.message,
                 "This NES ROM isn't Shadowgate (it's %ld bytes; expected %ld).", size, SUPPORTED_SIZE);
    } else {
        snprintf(r.message, sizeof r.message,
                 "This isn't a supported version. Supported: USA, Europe, Sweden, France, Germany.");
    }
    return r;
}
