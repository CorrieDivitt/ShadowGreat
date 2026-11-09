/* inflate.h - unpacking gzip (.gz) data, so the launcher can unpack the
 * Python download itself instead of running another program to do it. */
#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Decompress one gzip member. On success returns 0 and sets *out (malloc'd,
 * caller frees) and *out_size. The CRC-32 and length in the gzip trailer are
 * checked. Returns a negative number if the data is damaged. */
int gunzip(const uint8_t *in, size_t in_size, uint8_t **out, size_t *out_size);
#ifdef __cplusplus
}
#endif
