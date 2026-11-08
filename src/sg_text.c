/*
 * sg_text.c - message decoder (see sg_text.h and shadowgate-map.toml).
 */
#include "sg_text.h"
#include "cyc_mod.h"

#include <stdio.h>
#include <string.h>

/* ---- the two word lists, copied from the running game ---- */
static uint8_t s_words[0x1000];      /* $B000-$BFFF while bank 4 is in */
static bool    s_have_words;

bool sg_text_load_words(void)
{
    if (s_have_words) return true;
    if (cyc_mod_isolated() || !cyc_mod_isolate_begin()) return false;
    /* On a scratch copy of the machine: let the game swap in its word bank
     * with its own routine, read the lists, then throw the copy away. */
    CycModRegs r;
    cyc_mod_regs(&r);
    r.a = SG_WORD_BANK;
    bool ok = cyc_mod_call(SG_CODE_BANK_SWAP, &r);
    if (ok)
        for (unsigned i = 0; i < sizeof(s_words); ++i)
            s_words[i] = cyc_mod_peek((uint16_t)(SG_WORD_LIST_A + i));
    cyc_mod_isolate_end();
    s_have_words = ok;
    return ok;
}

void sg_text_forget_words(void) { s_have_words = false; }

/* ---- reading the bit stream ---- */
typedef struct {
    uint16_t addr;          /* next byte                                   */
    bool     from_words;    /* read from s_words instead of game memory    */
    uint8_t  byte;          /* byte being read                             */
    int      bits_left;     /* bits not yet read from `byte`               */
} Bits;

static uint8_t fetch(const Bits *b, uint16_t addr)
{
    if (b->from_words) {
        unsigned i = (unsigned)(addr - SG_WORD_LIST_A);
        return i < sizeof(s_words) ? s_words[i] : 0xFF;
    }
    return cyc_mod_peek(addr);
}

static unsigned read_bits(Bits *b, int n)
{
    unsigned v = 0;
    while (n-- > 0) {
        if (b->bits_left == 0) {
            b->byte = fetch(b, b->addr++);
            b->bits_left = 8;
        }
        v = (v << 1) | ((b->byte >> 7) & 1u);   /* highest bit first */
        b->byte = (uint8_t)(b->byte << 1);
        b->bits_left--;
    }
    return v;
}

/* ---- output ---- */
typedef struct { char *buf; size_t size, len; } Out;

static void put(Out *o, const char *s)
{
    while (*s && o->len + 1 < o->size) o->buf[o->len++] = *s++;
    o->buf[o->len] = 0;
}

/* Tile numbers from [text.chars] in shadowgate-map.toml */
static const char *tile_char(uint8_t t)
{
    static const char *digits[] = { "0","1","2","3","4","5","6","7","8","9" };
    static const char *letters[] = {
        "A","B","C","D","E","F","G","H","I","J","K","L","M",
        "N","O","P","Q","R","S","T","U","V","W","X","Y","Z" };
    if (t == 0x7F) return " ";
    if (t >= 0x80 && t <= 0x89) return digits[t - 0x80];
    if (t == 0x8A) return "!!";   /* one tile shows two marks */
    if (t == 0x8B) return "?";
    if (t == 0xCC) return ":";
    if (t == 0xCD) return ",";
    if (t == 0xCF) return "'";
    if (t == 0xD5) return "\"";
    if (t == 0xA1) return ".";
    if (t >= 0xB1 && t <= 0xCA) return letters[t - 0xB1];
    return NULL;
}

/* One character code: the game's own table turns it into a tile, and the
 * tile into a letter. A tile we haven't mapped yet is shown as {tXX}. */
static void put_code(Out *spoken, Out *logged, unsigned code)
{
    uint8_t tile = cyc_mod_peek((uint16_t)(SG_TABLE_CODE_TO_TILE + code));
    const char *c = tile_char(tile);
    if (c) {
        put(spoken, tile == 0x8A ? "!" : c);   /* the voice needs only one */
        put(logged, c);
    } else {
        char tmp[16];
        snprintf(tmp, sizeof tmp, "{t%02X}", tile);
        put(logged, tmp);
    }
}

static void put_word(Out *spoken, Out *logged, uint16_t list, unsigned n)
{
    if (!s_have_words) {
        char tmp[24];
        snprintf(tmp, sizeof tmp, "{word %c%u}", list == SG_WORD_LIST_A ? 'A' : 'B', n);
        put(logged, tmp);
        return;
    }
    Bits b = { (uint16_t)(list + n * SG_WORD_SLOT_BYTES), true, 0, 0 };
    for (int i = 0; i < 12; ++i) {            /* a slot holds at most 12 codes */
        unsigned c = read_bits(&b, 5);
        if (c == 0x1F) break;                 /* end of word */
        if (c == 0x1E) c = read_bits(&b, 5) + 0x20;
        put_code(spoken, logged, c);
    }
}

bool sg_text_decode(uint16_t addr, char *spoken, size_t spoken_size,
                    char *logged, size_t logged_size)
{
    Out s = { spoken, spoken_size, 0 }, l = { logged, logged_size, 0 };
    if (spoken_size) spoken[0] = 0;
    if (logged_size) logged[0] = 0;
    Bits b = { addr, false, 0, 0 };
    char tmp[32];

    for (int count = 0; count < 4000; ++count) { /* safety limit */
        unsigned c = read_bits(&b, 5);
        if (c == 0x1F) return true;              /* end of message */
        if (c < 0x1A) { put_code(&s, &l, c); continue; }   /* A-Z */
        switch (c) {
        case 0x1A: put(&s, " "); put(&l, " "); break;      /* space */
        case 0x1D: put(&s, " "); put(&l, " / "); break;    /* new line */
        case 0x1B: put(&s, "\f"); put(&l, " // "); break; /* end of a page: the game waits for A
                                                             (\f tells the voice to wait too) */
        case 0x1C: put(&l, "{1C}"); break;                 /* follows some page ends; not known yet */
        case 0x1E: {                                       /* escape */
            unsigned e = read_bits(&b, 5);
            if (e < 0x1C) {
                put_code(&s, &l, e + 0x20);                /* e.g. full stop */
            } else if (e == 0x1C) {
                put_word(&s, &l, SG_WORD_LIST_A, read_bits(&b, 8));
            } else if (e == 0x1D) {
                put_word(&s, &l, SG_WORD_LIST_B, read_bits(&b, 8));
            } else if (e == 0x1E) {
                /* Word number kept in RAM (e.g. the object you used); the
                 * message holds its address as two whole bytes, low byte
                 * first. The game ($C541) skips to the next whole byte. */
                b.bits_left = 0;
                unsigned lo = read_bits(&b, 8), hi = read_bits(&b, 8);
                uint16_t where = (uint16_t)(lo | (hi << 8));
                unsigned n = cyc_mod_peek(where);
                snprintf(tmp, sizeof tmp, "{ram %04X=%u}", where, n);
                put(&l, tmp);
                put_word(&s, &l, SG_WORD_LIST_A, n);
            } else {
                put(&l, "{esc1F: stopped}");               /* not known yet */
                return false;
            }
            break;
        }
        }
    }
    put(&l, "{too long: stopped}");
    return false;
}
