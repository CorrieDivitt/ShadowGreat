/*
 * feat_mouse.c - point and click (Phase 6).
 *
 * p6-coords: turn the Windows mouse position into game pixels (0-255
 * across, 0-239 down) whatever the window size or fullscreen, and find the
 * hit box under it.
 * p6-write: a left click writes what selects that thing - the cursor
 * position, the button bytes, or a menu's choice byte - using only values
 * from shadowgate-map.toml (via sg_hitboxes.c).
 * p6-click: then it holds the NES A button for a couple of frames, so the
 * game acts on that selection.
 * p6-message: in a message box a left click is SAVED and handed to the
 * game exactly when it next checks for A (two hook sites, see below), so
 * no click is ever lost: while printing it finishes the page, on a finished
 * page it moves on. Title screen: a click presses Start.
 * p6-hover: on menu screens, moving the pointer onto a choice moves the
 * game's highlight there (the play screen has no hover: verbs only light
 * up once selected).
 * Right click presses B, anywhere (user's request, 2026-11-08).
 * Everything is logged to shadowgate.log.
 */
#include "../sg_features.h"
#include "../sg_game.h"
#include "../sg_hitboxes.h"
#include "../sg_settings_link.h"
#include "cyc_mod.h"
#include "mod_function_hooks.h"
#include "mod_runtime.h"

#include <stdio.h>

#include <string.h>

#if defined(CYC_WITH_SDL)
#include <SDL.h>
#endif

static int         s_x = -1, s_y = -1;   /* game pixel under the mouse, -1 = off */
int sg_pointer_x = -1, sg_pointer_y = -1;  /* shared copy (sg_features.h) */
static const char *s_over;               /* hit box under the mouse, or NULL */
static bool        s_click_pending;      /* a left click waiting for the frame */
static int         s_click_x, s_click_y;
static uint8_t     s_press_buttons;      /* NES buttons we are holding */
static int         s_press_frames;       /* frames left to hold them */

/* NES controller byte bits (nesrecomp's cyc_host.c) */
#define NES_BUTTON_A      0x80
#define NES_BUTTON_B      0x40
#define NES_BUTTON_START  0x10
/* Hold a button this many frames. The game reads the pad once a frame and
 * acts on a new press; two frames make sure it is seen. */
#define HOLD_FRAMES 2

/* ---- message-box clicks (p6-message polish, user chose "every click
 * counts" 2026-11-08) ----
 * The game only sees A in the one frame it is newly pressed ($F5), and
 * while printing it looks once per letter, so most timed presses were
 * missed. Instead we set the A bit of $F5 right before the game reads it:
 *   $C7F4  LDA $F5  - per-letter check while printing (A = finish page)
 *   $C9F9  LDA $F5  - end-of-page wait (A = next page / close)
 * Both sites are in game.toml (CRC-32 of 16 bytes, no ROM bytes). */
#define RAM_PAD_PRESSED     0x00F5
#define HOOK_TEXT_A         "shadowgate.mouse.text-a"
#define HOOK_PAGE_A         "shadowgate.mouse.page-a"
#ifndef SITE_TEXT_A
#define SITE_TEXT_A         0xC7F4
#endif
#ifndef SITE_PAGE_A
#define SITE_PAGE_A         0xC9F9
#endif
#define CLICK_KEEP_FRAMES   60      /* a saved click is dropped after ~1 s */

static bool     s_msg_click;        /* a message-box click waiting for the game */
static uint32_t s_msg_click_frame;

static int give_saved_click(uint16_t addr)
{
    if (sg_feature_narration.enabled) sg_narration_print_site(addr);   /* page tracking */
    if (!sg_feature_mouse.enabled || !s_msg_click) return 0;
    s_msg_click = false;
    if (sg_game.frame - s_msg_click_frame > CLICK_KEEP_FRAMES) return 0;
    cyc_mod_poke(RAM_PAD_PRESSED, (uint8_t)(cyc_mod_peek(RAM_PAD_PRESSED) | 0x80));
    sg_log("frame %6u  mouse: saved click given to the game at $%04X (%s)",
           (unsigned)sg_game.frame, addr,
           addr == SITE_TEXT_A ? "finish the page" : "next page / close");
    return 0;   /* the game's own instruction runs as normal */
}

NES_MOD_CONSTRUCTOR(sg_register_mouse_hooks)
{
    nes_mod_register_function_entry_plugin(HOOK_TEXT_A, SITE_TEXT_A, give_saved_click);
    nes_mod_register_function_entry_plugin(HOOK_PAGE_A, SITE_PAGE_A, give_saved_click);
}

static void press(uint8_t buttons)
{
    s_press_buttons |= buttons;
    s_press_frames = HOLD_FRAMES;
}

/* RAM addresses (see shadowgate-map.toml) */
#define RAM_CURSOR_X       0x00AB
#define RAM_CURSOR_Y       0x00AC
#define RAM_CURSOR_X_WORK  0x0076
#define RAM_CURSOR_Y_WORK  0x0077
#define RAM_BUTTON         0x00AD
#define RAM_HOVERED        0x00AF
#define RAM_COMMAND        0x00B2
/* $AF modes from [buttons.scene] in shadowgate-map.toml */
#define AF_SCENE_FROM_VERBS 0x16
#define AF_SCENE_FROM_MOVE  0x17
#define AF_MAP              0x19
#define COMMAND_MOVE        0x08
/* [snap] scene_bounds: where the cursor may go inside the scene */
#define SCENE_LEFT   0x0F
#define SCENE_TOP    0x20
#define SCENE_RIGHT  0x7F
#define SCENE_BOTTOM 0x90

const char *sg_hitbox_at(SgScreen screen, int x, int y)
{
    for (int i = 0; i < sg_hitbox_count; ++i) {
        const SgHitbox *h = &sg_hitboxes[i];
        if (h->screen == screen && x >= h->left && x <= h->right &&
            y >= h->top && y <= h->bottom)
            return h->name;
    }
    return NULL;
}

#if defined(CYC_WITH_SDL)
/* Where the pointer is, in game pixels. The host draws the game with an
 * SDL "logical size" of 256 x 240, so SDL can undo the scaling and the black
 * bars for us. We ask for the raw window position (SDL_GetMouseState) and
 * convert it once, exactly as nesrecomp does for its Zapper. (The x/y inside
 * mouse EVENTS are already converted by SDL, so converting those again
 * shrank everything towards the top-left corner: the first test's bug.) */
static void pointer_in_game(int *x, int *y)
{
    *x = *y = -1;
    SDL_Window *win = SDL_GetMouseFocus();     /* NULL when outside the window */
    SDL_Renderer *ren = win ? SDL_GetRenderer(win) : NULL;
    if (!ren) return;
    int wx, wy;
    SDL_GetMouseState(&wx, &wy);
    float lx, ly;
    SDL_RenderWindowToLogical(ren, wx, wy, &lx, &ly);
    if (lx >= 0 && lx < 256 && ly >= 0 && ly < 240) { *x = (int)lx; *y = (int)ly; }
}
#endif

static const char *screen_box(int x, int y)
{
    if (x < 0 || sg_game.busy) return NULL;
    return sg_hitbox_at(sg_game.screen, x, y);
}

/* A click from somewhere else (the pad's A, feat_pad.c), in game pixels. */
void sg_mouse_click_at(int x, int y)
{
    s_click_x = x;
    s_click_y = y;
    s_click_pending = true;
}

static void mouse_event(const void *e)
{
#if defined(CYC_WITH_SDL)
    if (cyc_ui_menu_open()) return;
    const SDL_Event *ev = (const SDL_Event *)e;
    if (ev->type == SDL_MOUSEBUTTONDOWN && ev->button.button == SDL_BUTTON_LEFT) {
        int x, y;
        pointer_in_game(&x, &y);
        /* Acted on in mouse_frame, with this frame's screen reading. */
        s_click_x = x;
        s_click_y = y;
        s_click_pending = true;
    } else if (ev->type == SDL_MOUSEBUTTONDOWN && ev->button.button == SDL_BUTTON_RIGHT) {
        press(NES_BUTTON_B);
        sg_log("frame %6u  mouse: right click: pressing B  [screen: %s]",
               (unsigned)sg_game.frame, sg_screen_name(sg_game.screen));
    }
#else
    (void)e;
#endif
}

/* ---- writing to the game: only values from the map ---- */
static char s_wrote[160];

static void poke(uint16_t addr, uint8_t value)
{
    cyc_mod_poke(addr, value);
    size_t n = strlen(s_wrote);
    snprintf(s_wrote + n, sizeof s_wrote - n, "%s$%02X=%02X", n ? " " : "", addr, value);
}

static void put_cursor(uint8_t x, uint8_t y)
{
    /* both the shown position and the input loop's working copy */
    poke(RAM_CURSOR_X, x);      poke(RAM_CURSOR_Y, y);
    poke(RAM_CURSOR_X_WORK, x); poke(RAM_CURSOR_Y_WORK, y);
}

static const SgButton *find_button(const char *name)
{
    for (int i = 0; i < sg_button_count; ++i)
        if (!strcmp(sg_buttons[i].name, name)) return &sg_buttons[i];
    return NULL;
}

static const SgChoice *find_choice(SgScreen screen, const char *name)
{
    for (int i = 0; i < sg_choice_count; ++i)
        if (sg_choices[i].screen == screen && !strcmp(sg_choices[i].name, name))
            return &sg_choices[i];
    return NULL;
}

static uint8_t clamp(int v, int lo, int hi) { return (uint8_t)(v < lo ? lo : v > hi ? hi : v); }

static void apply_click(int x, int y)
{
    const char *box = screen_box(x, y);
    const char *what = "nothing to do";
    bool act = false;
    uint8_t a_press = 0;      /* a button to press without selecting anything */
    s_wrote[0] = 0;

    if (sg_settings_link_hit(x, y)) {
        what = sg_settings_link_open() ? "SETTINGS: opened the Shadowgreat options"
                                       : "SETTINGS: the in-game menu is not available";
    } else if (sg_game.busy) {
        what = "ignored: the game is changing screen";
    } else if (sg_game.screen == SG_SCREEN_MESSAGE) {
        /* Hit boxes are ignored here, so skipping text never picks a verb.
         * The click is saved for the game's next A check. */
        what = "message box: click saved for the game";
        s_msg_click = true;
        s_msg_click_frame = sg_game.frame;
    } else if (sg_game.screen == SG_SCREEN_TITLE) {
        what = "title screen: start";
        a_press = NES_BUTTON_START;
    } else if (!box) {
        /* empty space: write nothing */
    } else if (sg_game.screen == SG_SCREEN_PLAY && !strcmp(box, "SCENE")) {
        /* X/Y onto the object, and $AF set to the scene mode the d-pad
         * would give ($17 when MOVE is the command, else $16). Leaving $AF
         * on the last button made A press that button instead: after B
         * cancelled TAKE, a scene click re-highlighted TAKE without
         * selecting it (found by the user 2026-11-08). */
        put_cursor(clamp(x, SCENE_LEFT, SCENE_RIGHT), clamp(y, SCENE_TOP, SCENE_BOTTOM));
        poke(RAM_HOVERED, cyc_mod_peek(RAM_COMMAND) == COMMAND_MOVE
                              ? AF_SCENE_FROM_MOVE : AF_SCENE_FROM_VERBS);
        what = "scene: cursor moved";
        act = true;
    } else if (sg_game.screen == SG_SCREEN_PLAY && !strcmp(box, "MAP")) {
        /* The map is its own input mode: X/Y alone is ignored until $AF is
         * 0x19 (found by the user 2026-11-08: map clicks only worked after
         * entering the map once with the d-pad, which sets $AF = 0x19). */
        put_cursor((uint8_t)x, (uint8_t)y);
        poke(RAM_HOVERED, AF_MAP);
        what = "map: cursor moved";
        act = true;
    } else if (sg_game.screen == SG_SCREEN_PLAY) {
        const SgButton *b = find_button(box);
        if (b) {
            put_cursor(b->x, b->y);
            if (b->ad >= 0) poke(RAM_BUTTON, (uint8_t)b->ad);
            poke(RAM_HOVERED, b->af);
            what = "button selected";
            act = true;
        }
    } else {
        const SgChoice *c = find_choice(sg_game.screen, box);
        if (c) {
            poke(c->byte, c->value);
            what = "menu choice";
            act = true;
        }
    }
    if (act) a_press = NES_BUTTON_A;
    if (a_press) press(a_press);         /* held in mouse_input from next frame */
    sg_log("frame %6u  mouse: click at (%d,%d) on %s [%s]: %s%s%s%s  ($AF now %02X)",
           (unsigned)sg_game.frame, x, y, box ? box : "nothing",
           sg_screen_name(sg_game.screen), what,
           s_wrote[0] ? "; wrote " : "", s_wrote,
           a_press == NES_BUTTON_A ? "; pressing A" : a_press ? "; pressing Start" : "",
           cyc_mod_peek(RAM_HOVERED));
}

/* p6-hover. Write a menu's choice byte when the pointer has MOVED onto a
 * different choice. Only on movement, so the keyboard and pad still work
 * while the mouse sits still; only on a new choice, so we never fight the
 * player's own key presses; never over empty space; only listed values. */
static void hover(const char *box, bool moved)
{
    if (!moved || !box || sg_game.busy) return;
    const SgChoice *c = find_choice(sg_game.screen, box);   /* menus only */
    if (!c || cyc_mod_peek(c->byte) == c->value) return;
    cyc_mod_poke(c->byte, c->value);
    sg_log("frame %6u  mouse: hover highlights %s [%s]; wrote $%02X=%02X",
           (unsigned)sg_game.frame, box, sg_screen_name(sg_game.screen),
           c->byte, c->value);
}

/* Once a frame: act on a click, highlight menu choices under the pointer,
 * and log when the box under the mouse changes. */
static void mouse_frame(void)
{
    if (cyc_ui_menu_open()) {
        s_click_pending = false;
        sg_pointer_x = -1;
        sg_pointer_y = -1;
        return;
    }
    static int prev_x = -1, prev_y = -1;
#if defined(CYC_WITH_SDL)
    pointer_in_game(&s_x, &s_y);
#endif
    sg_pointer_x = s_x;
    sg_pointer_y = s_y;
    /* A saved message click never outlives the message box. */
    if (s_msg_click && !sg_game.busy && sg_game.screen != SG_SCREEN_MESSAGE)
        s_msg_click = false;
    bool moved = s_x != prev_x || s_y != prev_y;
    prev_x = s_x;
    prev_y = s_y;
    if (s_click_pending) {
        s_click_pending = false;
        apply_click(s_click_x, s_click_y);
    }
    const char *box = screen_box(s_x, s_y);
    if (box == s_over) return;
    hover(box, moved);
    sg_log("frame %6u  mouse: (%d,%d) over %s  [screen: %s]",
           (unsigned)sg_game.frame, s_x, s_y, box ? box : "nothing",
           sg_screen_name(sg_game.screen));
    s_over = box;
}

/* Every frame, before the game reads its controller: add our presses to
 * whatever the keyboard or pad is pressing. */
static void mouse_input(uint8_t buttons[2])
{
    if (s_press_frames > 0) {
        buttons[0] |= s_press_buttons;
        if (--s_press_frames == 0) s_press_buttons = 0;
    }
}

static void mouse_start(void)
{
    s_over = NULL;
    s_click_pending = false;
    s_press_buttons = 0;
    s_press_frames = 0;
    s_msg_click = false;
    if (!nes_mod_set_function_hook_enabled(HOOK_TEXT_A, 1) ||
        !nes_mod_set_function_hook_enabled(HOOK_PAGE_A, 1))
        sg_log("mouse: could not switch on the message-click hooks");
    sg_log("mouse: point and click (left = select + A, right = B)");
}

/* Switched Off in the menu: drop anything half-done, give the pointer back. */
static void mouse_stop(void)
{
    s_over = NULL;
    s_click_pending = false;
    s_press_buttons = 0;
    s_press_frames = 0;
    s_msg_click = false;
    sg_pointer_x = sg_pointer_y = -1;
#if defined(CYC_WITH_SDL)
    SDL_ShowCursor(SDL_ENABLE);
#endif
}

SgFeature sg_feature_mouse = {
    .key = "Mouse",
    .default_on = true,
    .start = mouse_start,
    .stop = mouse_stop,
    .draw = sg_settings_link_draw,
    .frame = mouse_frame,
    .event = mouse_event,
    .input = mouse_input,
};
