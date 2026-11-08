/*
 * feat_mouse.c - point and click (Phase 6).
 *
 * p6-coords (this step): turn the Windows mouse position into game pixels
 * (0-255 across, 0-239 down) whatever the window size or fullscreen, look up
 * which hit box it is over, and write changes and clicks to shadowgate.log.
 * It does NOT touch the game yet: no memory writes, no button presses.
 */
#include "../sg_features.h"
#include "../sg_game.h"
#include "../sg_hitboxes.h"

#include <string.h>

#if defined(CYC_WITH_SDL)
#include <SDL.h>
#endif

static int         s_x = -1, s_y = -1;   /* game pixel under the mouse, -1 = off */
static const char *s_over;               /* hit box under the mouse, or NULL */

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
        const char *box = screen_box(x, y);
        sg_log("frame %6u  mouse: click at (%d,%d) on %s  [screen: %s%s]",
               (unsigned)sg_game.frame, x, y, box ? box : "nothing",
               sg_screen_name(sg_game.screen), sg_game.busy ? ", busy" : "");
    }
#else
    (void)e;
#endif
}

/* Once a frame: log when the box under the mouse changes. */
static void mouse_frame(void)
{
#if defined(CYC_WITH_SDL)
    pointer_in_game(&s_x, &s_y);
#endif
    const char *box = screen_box(s_x, s_y);
    if (box == s_over) return;
    sg_log("frame %6u  mouse: (%d,%d) over %s  [screen: %s]",
           (unsigned)sg_game.frame, s_x, s_y, box ? box : "nothing",
           sg_screen_name(sg_game.screen));
    s_over = box;
}

static void mouse_start(void)
{
    s_over = NULL;
    sg_log("mouse: tracking the pointer (logging only, no clicks sent yet)");
}

SgFeature sg_feature_mouse = {
    .key = "Mouse",
    .default_on = true,
    .start = mouse_start,
    .frame = mouse_frame,
    .event = mouse_event,
};
