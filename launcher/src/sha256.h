/* sha256.h - SHA-256 (FIPS 180-4), to check downloads against known fingerprints. */
#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { uint32_t h[8]; uint64_t length; uint8_t block[64]; size_t used; } Sha256;
void sha256_init(Sha256 *s);
void sha256_update(Sha256 *s, const void *data, size_t size);
void sha256_hex(Sha256 *s, char out[65]);           /* finishes; lower-case hex */
/* The SHA-256 of a file (UTF-8 path), or 0 if it can't be read. */
int sha256_file(const char *path, char out[65]);
#ifdef __cplusplus
}
#endif
