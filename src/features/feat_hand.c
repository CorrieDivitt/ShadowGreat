/*
 * feat_hand.c - the game's hand is the mouse pointer, on every screen
 * (p6-gamehand).
 *
 * Config switch: HandFollowsMouse (default On); needs Mouse = On too.
 *
 * How it works:
 *  - The build reads the game's 4 hand tiles from YOUR ROM into the build
 *    folder (CMakeLists.txt -> generated/sg_hand_tiles.h). Nothing from the
 *    game is in the project; the tile location is in shadowgate-map.toml.
 *  - Every frame we hand the window our own copy of the picture
 *    (sg_host.c composes it; this feature draws last), with the hand on top at the mouse position:
 *    title, menus, message boxes, screen changes - everywhere, above all
 *    sprites. The Windows pointer is hidden while it is over the picture.
 *  - Where the game draws its own hand (OAM tiles $7A-$7D, palette 2) we
 *    paint the background back over it, so there are never two hands.
 *  - Press a direction on the keyboard / pad and the game's own hand comes
 *    back (and ours hides) until the mouse moves again.
 * Nothing here writes to the game's memory; clicks are feat_mouse.c's job.
 */
#include "../sg_features.h"
#include "../sg_game.h"
#include "cyc_core.h"
#include "cyc_render.h"
#include "sg_hand_tiles.h"   /* generated from the player's ROM at build time */

#include <stdlib.h>
#include <string.h>

#if defined(CYC_WITH_SDL)
#include <SDL.h>
#endif

/* nesrecomp's NES colour table (runner/cyc/hw_internal.h), so our hand has
 * exactly the colours the game's picture uses. */
extern uint32_t hw_palette_argb[512];

/* From shadowgate-map.toml [hand_cursor] */
#define HAND_FIRST_TILE   0x7A      /* tiles $7A $7B / $7C $7D, 2 x 2 */
#define HAND_PALETTE      2         /* sprite palette */
#define HAND_TIP_X        5         /* fingertip inside the 16 x 16 picture */
#define HAND_TIP_Y        0
static const uint8_t HAND_COLORS[4] = { 0x0F, 0x0F, 0x30, 0x30 };  /* 0 = see-through */
/* (Mesen's palette viewer said 0x26 for the last one, but the hand is
 * white on screen: corrected by the user 2026-11-08.) */

static const uint8_t s_chr[64] = SG_HAND_CHR_BYTES;
static uint8_t  s_hand[16][16];        /* pixel values 0-3, 0 = see-through */
static bool     s_mouse_mode;          /* true: our hand; false: the game's */
static int      s_prev_x = -1, s_prev_y = -1;
static uint32_t s_bg[256 * 240];

static void decode_hand(void)
{
    for (int t = 0; t < 4; ++t)                    /* 7A 7B / 7C 7D */
        for (int r = 0; r < 8; ++r)
            for (int c = 0; c < 8; ++c) {
                const uint8_t *tile = s_chr + 16 * t;
                int lo = (tile[r] >> (7 - c)) & 1, hi = (tile[r + 8] >> (7 - c)) & 1;
                s_hand[(t >> 1) * 8 + r][(t & 1) * 8 + c] = (uint8_t)(lo | hi << 1);
            }
}

static uint32_t hand_color(int value) { return hw_palette_argb[HAND_COLORS[value] & 0x3F]; }

static void show_windows_pointer(bool show)
{
#if defined(CYC_WITH_SDL)
    int want = show ? SDL_ENABLE : SDL_DISABLE;
    if (SDL_ShowCursor(SDL_QUERY) != want) SDL_ShowCursor(want);
#else
    (void)show;
#endif
}

/* Paint the background back where the game drew its hand this frame. */
static void erase_game_hand(uint32_t *out, int width, int x0)
{
    if (!cyc_render_background(s_bg)) return;
    const uint8_t *oam = cyc_ppu_oam();
    for (int slot = 0; slot < 64; ++slot) {
        const uint8_t *s = oam + 4 * slot;
        int tile = s[1] - HAND_FIRST_TILE;
        if (tile < 0 || tile > 3 || (s[2] & 3) != HAND_PALETTE || (s[2] & 0xC0)) continue;
        int sx = s[3], sy = s[0] + 1;
        for (int r = 0; r < 8; ++r)
            for (int c = 0; c < 8; ++c) {
                int v = s_hand[(tile >> 1) * 8 + r][(tile & 1) * 8 + c];
                int x = sx + c, y = sy + r;
                if (!v || x > 255 || y > 239) continue;
                /* Where the hand's tile has a pixel and the picture differs
                 * from the background, that pixel is the game's hand. */
                uint32_t *px = &out[y * width + x0 + x];
                *px = s_bg[y * 256 + x];
            }
    }
}

static void draw_hand(uint32_t *out, int width, int height, int x0, int tip_x, int tip_y)
{
    for (int r = 0; r < 16; ++r)
        for (int c = 0; c < 16; ++c) {
            int v = s_hand[r][c];
            int x = tip_x - HAND_TIP_X + c, y = tip_y - HAND_TIP_Y + r;
            if (!v || x < 0 || x > 255 || y < 0 || y >= height) continue;
            out[y * width + x0 + x] = hand_color(v);
        }
}

static void hand_draw(uint32_t *buf, int width, int height, int x0)
{
    if (!sg_feature_mouse.enabled || !s_mouse_mode) return;   /* the game's own hand */
    erase_game_hand(buf, width, x0);
    if (sg_pointer_x >= 0) draw_hand(buf, width, height, x0, sg_pointer_x, sg_pointer_y);
}

static void hand_frame(void)
{
    if (!sg_feature_mouse.enabled) return;
    int x = sg_pointer_x, y = sg_pointer_y;
    if (x >= 0 && (x != s_prev_x || y != s_prev_y) && !s_mouse_mode) {
        s_mouse_mode = true;
        sg_log("frame %6u  hand: mouse moved - showing our hand", (unsigned)sg_game.frame);
    }
    s_prev_x = x;
    s_prev_y = y;
    show_windows_pointer(x < 0);    /* hidden whenever it is over the picture */
}

/* A direction from the keyboard or pad hands the hand back to the game. */
static void hand_input(uint8_t buttons[2])
{
    if ((buttons[0] & 0x0F) && s_mouse_mode) {      /* Up, Down, Left, Right */
        s_mouse_mode = false;
        sg_log("frame %6u  hand: direction pressed - showing the game's hand", (unsigned)sg_game.frame);
    }
}

/* Switched Off in the menu: the game's own hand and the Windows pointer return. */
static void hand_stop(void)
{
    show_windows_pointer(true);
}

static void hand_start(void)
{
    decode_hand();
    s_mouse_mode = true;
    s_prev_x = s_prev_y = -1;
    cyc_render_capture_background(true);   /* needed to paint over the game's hand */
    sg_log(sg_feature_mouse.enabled ? "hand: drawn at the mouse on every screen"
                                    : "hand: needs Mouse = On; doing nothing");
}

SgFeature sg_feature_hand = {
    .key = "HandFollowsMouse",
    .default_on = true,
    .start = hand_start,
    .stop = hand_stop,
    .frame = hand_frame,
    .input = hand_input,
    .draw = hand_draw,
};
