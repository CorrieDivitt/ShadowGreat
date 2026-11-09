/* inflate.c - see inflate.h. A small DEFLATE decoder (RFC 1951) with the gzip
 * wrapper (RFC 1952), written for this project. Speed doesn't matter here:
 * it runs once, on a 22 MB download. */
#include "inflate.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    const uint8_t *in; size_t in_size, pos;
    uint32_t bitbuf; int bitcnt;
    uint8_t *out; size_t out_size, out_cap;
    int error;
} State;

typedef struct { short count[16]; short symbol[320]; } Huffman;

static int bits(State *s, int need)
{
    uint32_t v = s->bitbuf;
    while (s->bitcnt < need) {
        if (s->pos >= s->in_size) { s->error = -1; return 0; }
        v |= (uint32_t)s->in[s->pos++] << s->bitcnt;
        s->bitcnt += 8;
    }
    s->bitbuf = v >> need;
    s->bitcnt -= need;
    return (int)(v & ((1u << need) - 1));
}

static int put(State *s, uint8_t b)
{
    if (s->out_size == s->out_cap) {
        size_t cap = s->out_cap ? s->out_cap * 2 : 1 << 20;
        uint8_t *p = (uint8_t *)realloc(s->out, cap);
        if (!p) { s->error = -2; return 0; }
        s->out = p; s->out_cap = cap;
    }
    s->out[s->out_size++] = b;
    return 1;
}

/* Canonical Huffman decoding: codes of each length are consecutive. */
static int decode(State *s, const Huffman *h)
{
    int code = 0, first = 0, index = 0;
    for (int len = 1; len < 16; ++len) {
        code |= bits(s, 1);
        if (s->error) return -1;
        int count = h->count[len];
        if (code - count < first) return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

/* Returns 0 for a complete code, > 0 incomplete, < 0 over-subscribed. */
static int build(Huffman *h, const short *length, int n)
{
    short offs[16];
    memset(h->count, 0, sizeof h->count);
    for (int i = 0; i < n; ++i) h->count[length[i]]++;
    if (h->count[0] == n) return 0;
    int left = 1;
    for (int len = 1; len < 16; ++len) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0) return left;
    }
    offs[1] = 0;
    for (int len = 1; len < 15; ++len) offs[len + 1] = (short)(offs[len] + h->count[len]);
    for (int i = 0; i < n; ++i) if (length[i]) h->symbol[offs[length[i]]++] = (short)i;
    return left;
}

static const short LBASE[29] = { 3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258 };
static const short LEXT[29]  = { 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0 };
static const short DBASE[30] = { 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,
                                 4097,6145,8193,12289,16385,24577 };
static const short DEXT[30]  = { 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13 };

static int codes(State *s, const Huffman *lens, const Huffman *dists)
{
    for (;;) {
        int sym = decode(s, lens);
        if (sym < 0 || s->error) return -3;
        if (sym < 256) { if (!put(s, (uint8_t)sym)) return -2; continue; }
        if (sym == 256) return 0;
        sym -= 257;
        if (sym >= 29) return -3;
        int len = LBASE[sym] + bits(s, LEXT[sym]);
        int dsym = decode(s, dists);
        if (dsym < 0 || dsym >= 30 || s->error) return -3;
        size_t dist = (size_t)(DBASE[dsym] + bits(s, DEXT[dsym]));
        if (s->error || dist > s->out_size) return -3;
        while (len--) if (!put(s, s->out[s->out_size - dist])) return -2;
    }
}

static int stored(State *s)
{
    s->bitbuf = 0; s->bitcnt = 0;                     /* to the next byte */
    if (s->pos + 4 > s->in_size) return -1;
    unsigned len = s->in[s->pos] | s->in[s->pos + 1] << 8;
    unsigned nlen = s->in[s->pos + 2] | s->in[s->pos + 3] << 8;
    s->pos += 4;
    if (len != (~nlen & 0xffff) || s->pos + len > s->in_size) return -1;
    while (len--) if (!put(s, s->in[s->pos++])) return -2;
    return 0;
}

static int fixed(State *s)
{
    static Huffman lens, dists;
    static int ready;
    if (!ready) {
        short l[288];
        int i = 0;
        for (; i < 144; ++i) l[i] = 8;
        for (; i < 256; ++i) l[i] = 9;
        for (; i < 280; ++i) l[i] = 7;
        for (; i < 288; ++i) l[i] = 8;
        build(&lens, l, 288);
        for (i = 0; i < 30; ++i) l[i] = 5;
        build(&dists, l, 30);
        ready = 1;
    }
    return codes(s, &lens, &dists);
}

static int dynamic(State *s)
{
    static const short order[19] = { 16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15 };
    short lengths[320];
    Huffman lens, dists;
    int nlen = bits(s, 5) + 257, ndist = bits(s, 5) + 1, ncode = bits(s, 4) + 4;
    if (s->error || nlen > 286 || ndist > 30) return -4;
    int i = 0;
    for (; i < ncode; ++i) lengths[order[i]] = (short)bits(s, 3);
    for (; i < 19; ++i) lengths[order[i]] = 0;
    if (s->error || build(&lens, lengths, 19) != 0) return -4;
    for (i = 0; i < nlen + ndist;) {
        int sym = decode(s, &lens);
        if (sym < 0 || s->error) return -4;
        if (sym < 16) { lengths[i++] = (short)sym; continue; }
        short value = 0;
        int repeat;
        if (sym == 16) {
            if (i == 0) return -4;
            value = lengths[i - 1];
            repeat = 3 + bits(s, 2);
        } else if (sym == 17) repeat = 3 + bits(s, 3);
        else repeat = 11 + bits(s, 7);
        if (s->error || i + repeat > nlen + ndist) return -4;
        while (repeat--) lengths[i++] = value;
    }
    if (lengths[256] == 0) return -4;
    int err = build(&lens, lengths, nlen);
    if (err && (err < 0 || nlen != lens.count[0] + lens.count[1])) return -4;
    err = build(&dists, lengths + nlen, ndist);
    if (err && (err < 0 || ndist != dists.count[0] + dists.count[1])) return -4;
    return codes(s, &lens, &dists);
}

static uint32_t crc32_of(const uint8_t *p, size_t n)
{
    static uint32_t table[256];
    static int ready;
    if (!ready) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        ready = 1;
    }
    uint32_t c = 0xffffffffu;
    while (n--) c = table[(c ^ *p++) & 0xff] ^ (c >> 8);
    return c ^ 0xffffffffu;
}

int gunzip(const uint8_t *in, size_t in_size, uint8_t **out, size_t *out_size)
{
    *out = NULL; *out_size = 0;
    if (in_size < 18 || in[0] != 0x1f || in[1] != 0x8b || in[2] != 8) return -10;
    int flags = in[3];
    size_t pos = 10;
    if (flags & 4) { if (pos + 2 > in_size) return -10; pos += 2 + (in[pos] | in[pos + 1] << 8); }
    if (flags & 8) { while (pos < in_size && in[pos]) ++pos; ++pos; }
    if (flags & 16) { while (pos < in_size && in[pos]) ++pos; ++pos; }
    if (flags & 2) pos += 2;
    if (pos >= in_size) return -10;

    State s;
    memset(&s, 0, sizeof s);
    s.in = in; s.in_size = in_size - 8; s.pos = pos;     /* the last 8 bytes are the trailer */
    int last, err = 0;
    do {
        last = bits(&s, 1);
        int type = bits(&s, 2);
        if (s.error) { err = -1; break; }
        err = type == 0 ? stored(&s) : type == 1 ? fixed(&s) : type == 2 ? dynamic(&s) : -5;
        if (s.error) err = s.error;
    } while (!err && !last);
    if (!err) {
        const uint8_t *t = in + in_size - 8;
        uint32_t crc = (uint32_t)t[0] | (uint32_t)t[1] << 8 | (uint32_t)t[2] << 16 | (uint32_t)t[3] << 24;
        uint32_t size = (uint32_t)t[4] | (uint32_t)t[5] << 8 | (uint32_t)t[6] << 16 | (uint32_t)t[7] << 24;
        if (size != (uint32_t)s.out_size || crc != crc32_of(s.out, s.out_size)) err = -11;
    }
    if (err) { free(s.out); return err; }
    *out = s.out;
    *out_size = s.out_size;
    return 0;
}
