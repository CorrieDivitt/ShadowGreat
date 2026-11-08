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
 * p6-message: in a message box any left click just presses A (advance the
 * text); on the title screen it presses Start.
 * p6-hover: on menu screens, moving the pointer onto a choice moves the
 * game's highlight there (the play screen has no hover: verbs only light
 * up once selected).
 * Right click presses B, anywhere (user's request, 2026-11-08).
 * Everything is logged to shadowgate.log.
 */
#include "../sg_features.h"
#include "../sg_game.h"
#include "../sg_hitboxes.h"
#include "cyc_mod.h"

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

static void mouse_event(const void *e)
{
#if defined(CYC_WITH_SDL)
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

    if (sg_game.busy) {
        what = "ignored: the game is changing screen";
    } else if (sg_game.screen == SG_SCREEN_MESSAGE) {
        /* Hit boxes are ignored here, so skipping text never picks a verb. */
        what = "message box: advance";
        a_press = NES_BUTTON_A;
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
    static int prev_x = -1, prev_y = -1;
#if defined(CYC_WITH_SDL)
    pointer_in_game(&s_x, &s_y);
#endif
    sg_pointer_x = s_x;
    sg_pointer_y = s_y;
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
    sg_log("mouse: point and click (left = select + A, right = B)");
}

SgFeature sg_feature_mouse = {
    .key = "Mouse",
    .default_on = true,
    .start = mouse_start,
    .frame = mouse_frame,
    .event = mouse_event,
    .input = mouse_input,
};
