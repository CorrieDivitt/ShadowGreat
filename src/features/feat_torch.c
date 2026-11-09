/*
 * feat_torch.c - the torch tracker (p10-torch): how many actions each lit
 * torch has left, shown above its torch on the play screen.
 *
 * Config switch: TorchTracker (default Off).
 *
 *  - The counts are the game's own: $04F4 (left torch) and $04F7 (right
 *    torch), read once a frame (shadowgate-map.toml [[ram]]). A new torch
 *    starts at 60. Nothing is written to the game's memory.
 *  - The number sits in the black strip above the inventory panel, centred
 *    over its torch, in the panel's tan. With 15 or fewer actions left it
 *    turns orange (when the flame gets smaller), and with 7 or fewer the
 *    red of the torch flame (when the game's warning music starts).
 *    Both lit torches count down together, one per action. A torch whose
 *    count is 0 shows nothing (nor does anything over 99, which is not a
 *    torch count: $04F7 holds 240 at power-on).
 *  - It's drawn with the game's own digits (message letters $80-$89). The
 *    play screen switches graphics banks part way down, and the bank we can
 *    read is the one for the bottom part (verbs and message box, the same
 *    lettering), not the inventory panel's. So before using the digits we
 *    check that the verb letters on screen (LOOK, OPEN, ...) match the
 *    shapes we read, and that the ten digits look like digits (not blank,
 *    not a solid block, all different). The first time that passes we keep
 *    a copy of the ten shapes and use it from then on, so a message (which
 *    changes the graphics banks while it prints) can't spoil them. Until
 *    then, or if it never passes, we draw our own plain digits.
 *    Positions and colours: shadowgate-map.toml [torch_tracker].
 */
#include "../sg_features.h"
#include "../sg_game.h"
#include "cyc_mod.h"
#include "cyc_render.h"

#include <string.h>

/* nesrecomp's NES colour table (also used by feat_hand.c). */
extern uint32_t hw_palette_argb[512];

#define RAM_LEFT        0x04F4
#define RAM_RIGHT       0x04F7
#define LOW_AT          15       /* this many actions left or fewer: orange  */
#define WARN_AT         7        /* this many actions left or fewer: red     */
#define COLOR_NORMAL    0x37     /* the panel's tan                          */
#define COLOR_LOW       0x27     /* orange                                   */
#define COLOR_WARN      0x16     /* the torch flame's red                    */
#define NUMBER_TOP      8        /* rows 8-15: the black strip (0-16) above the panel */
#define LEFT_CENTRE     148      /* torch hit boxes: x 143-152 and 231-240   */
#define RIGHT_CENTRE    236
#define DIGIT_TILE_0    0x80     /* message lettering: $80-$89 = 0-9         */

static int s_left, s_right;      /* read in frame(), drawn in draw()         */
static uint8_t s_digits[10][8];  /* the game's digit shapes, once captured   */
static bool s_have_digits;
static bool s_shown;             /* on a screen with the panel               */

static void torch_start(void)
{
    s_left = s_right = -1;
    s_shown = false;
    sg_log("torch tracker: on");
}

static void torch_stop(void)
{
    s_shown = false;
}

static void torch_frame(void)
{
    int left = cyc_mod_peek(RAM_LEFT), right = cyc_mod_peek(RAM_RIGHT);
    if (left != s_left || right != s_right)
        sg_log("torch tracker: left %d, right %d", left, right);
    s_left = left;
    s_right = right;
    s_shown = !sg_game.busy && (sg_game.screen == SG_SCREEN_PLAY || sg_game.screen == SG_SCREEN_MESSAGE);
}

/* ---- are the game's letters where we're reading them? -------------------- */
static bool glyph_matches(const uint32_t *buf, int width, int x0, int sx, int sy, uint16_t pattern)
{
    uint32_t ink = 0;
    bool have_ink = false;
    for (int r = 0; r < 8; ++r) {
        uint8_t bits = (uint8_t)(cyc_render_chr((uint16_t)(pattern + r)) | cyc_render_chr((uint16_t)(pattern + r + 8)));
        for (int c = 0; c < 8; ++c) {
            if (!((bits >> (7 - c)) & 1)) continue;
            uint32_t p = buf[(sy + r) * width + x0 + sx + c];
            if (!have_ink) { ink = p; have_ink = true; }
            else if (p != ink) return false;
        }
    }
    if (!have_ink) return false;
    for (int r = 0; r < 8; ++r) {
        uint8_t bits = (uint8_t)(cyc_render_chr((uint16_t)(pattern + r)) | cyc_render_chr((uint16_t)(pattern + r + 8)));
        for (int c = 0; c < 8; ++c)
            if (!((bits >> (7 - c)) & 1) && buf[(sy + r) * width + x0 + sx + c] == ink) return false;
    }
    return true;
}

/* Compare the letters in the bottom part of the screen (the verbs, or a
 * message: rows 19-28) with the shapes the pattern table gives for them. */
static bool game_digits_usable(const uint32_t *buf, int width, int height, int x0)
{
    static const uint16_t tables[] = { 0x2000, 0x2400 };
    if (height < 232) return false;
    for (size_t t = 0; t < sizeof tables / sizeof tables[0]; ++t) {
        int good = 0, bad = 0;
        for (int row = 19; row <= 28; ++row)
            for (int col = 1; col <= 30; ++col) {
                uint8_t tile = cyc_render_nametable((uint16_t)(tables[t] + row * 32 + col));
                if (tile < 0x80 || tile > 0xCA) continue;            /* letters and digits only */
                uint16_t pattern = (uint16_t)(cyc_render_line_bg_table(row * 8) + tile * 16);
                if (glyph_matches(buf, width, x0, col * 8, row * 8, pattern)) ++good;
                else ++bad;
            }
        if (good >= 3 && good > bad) return true;
    }
    return false;
}

/* Copy the ten digit shapes if they look like digits. */
static bool capture_digits(uint16_t table)
{
    uint8_t d[10][8];
    for (int n = 0; n < 10; ++n) {
        int set = 0;
        for (int r = 0; r < 8; ++r) {
            uint16_t p = (uint16_t)(table + (DIGIT_TILE_0 + n) * 16 + r);
            d[n][r] = (uint8_t)(cyc_render_chr(p) | cyc_render_chr((uint16_t)(p + 8)));
            for (int c = 0; c < 8; ++c) set += (d[n][r] >> c) & 1;
        }
        if (set < 6 || set > 40) return false;                   /* blank or a block */
        for (int m = 0; m < n; ++m)
            if (!memcmp(d[m], d[n], 8)) return false;            /* two the same     */
    }
    memcpy(s_digits, d, sizeof d);
    return true;
}

/* ---- our own plain digits, used only if the game's can't be read --------- */
static const uint8_t PLAIN[10][7] = {     /* 5 x 7, one byte per row, bit 4 = left */
    { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E }, { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E },
    { 0x0E, 0x11, 0x01, 0x06, 0x08, 0x10, 0x1F }, { 0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E },
    { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 }, { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E },
    { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E }, { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 },
    { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E }, { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C },
};

static void draw_number(uint32_t *buf, int width, int x0, int centre, int value)
{
    char text[8];
    int n = 0;
    if (value <= 0 || value > 99) return;
    for (int v = value; v > 0 && n < 3; v /= 10) text[n++] = (char)(v % 10);
    uint32_t color = hw_palette_argb[value <= WARN_AT ? COLOR_WARN : value <= LOW_AT ? COLOR_LOW : COLOR_NORMAL];
    int left = centre - n * 4;
    for (int i = 0; i < n; ++i) {
        int digit = text[n - 1 - i], x = left + i * 8;
        for (int r = 0; r < 8; ++r) {
            uint8_t bits;
            if (s_have_digits) {
                bits = s_digits[digit][r];
            } else {
                bits = r < 7 ? (uint8_t)(PLAIN[digit][r] << 2) : 0;   /* 5 wide, centred in 8 */
            }
            for (int c = 0; c < 8; ++c)
                if ((bits >> (7 - c)) & 1) buf[(NUMBER_TOP + r) * width + x0 + x + c] = color;
        }
    }
}

static void torch_draw(uint32_t *buf, int width, int height, int x0)
{
    if (!s_shown || height < 240 || ((s_left <= 0 || s_left > 99) && (s_right <= 0 || s_right > 99))) return;
    /* Capture the game's digits once, on the plain play screen (no message). */
    if (!s_have_digits && sg_game.screen == SG_SCREEN_PLAY && game_digits_usable(buf, width, height, x0)
        && capture_digits(cyc_render_line_bg_table(200))) {
        s_have_digits = true;
        sg_log("torch tracker: using the game's digits");
    }
    draw_number(buf, width, x0, LEFT_CENTRE, s_left);
    draw_number(buf, width, x0, RIGHT_CENTRE, s_right);
}

SgFeature sg_feature_torch = {
    .key = "TorchTracker",
    .default_on = false,
    .start = torch_start,
    .stop = torch_stop,
    .frame = torch_frame,
    .draw = torch_draw,
};
