/*
 * feat_pad.c - a gamepad stick moves the mouse pointer (p10-controller).
 *
 * Config: PadPointer = On/Off (default On), PointerStick = Left/Right
 * (default Left), PointerSpeed = 1-10 (default 5). Needs Mouse = On.
 *
 * nesrecomp already plays the game with a pad (buttons are set on its start
 * screen) and makes the LEFT stick work the D-pad as well. This feature adds:
 *  - The chosen stick moves the real mouse pointer inside the game window,
 *    so everything the mouse does (the hand pointer, menu highlights, clicks
 *    through feat_mouse.c) works the same with the stick.
 *  - While the pointer was last moved with the stick, the pad button that
 *    is the NES A (config.ini [Gamepad.Player1] a = ..., reassignable on the
 *    game's start screen) clicks where it points, like a left click, instead
 *    of being the NES A. After a D-pad or keyboard direction, it's the NES A
 *    again. B is always B (which is what a right click does anyway).
 *  - Pressing either stick in is A too: a click while pointing, else the
 *    NES A. Not while Back is held (Back + stick press are nesrecomp's save
 *    state and rewind shortcuts).
 *  - With the left stick chosen, its D-pad directions are taken back out, so
 *    the stick only moves the pointer; the D-pad itself still works.
 * Nothing here writes to the game's memory.
 */
#include "../sg_features.h"
#include "../sg_game.h"
#include "cyc_settings.h"

#include <math.h>
#include <string.h>

#if defined(CYC_WITH_SDL)
#include <SDL.h>
#endif

#define NES_A          0x80
#define NES_UP         0x08
#define NES_DOWN       0x04
#define NES_LEFT       0x02
#define NES_RIGHT      0x01
#define DEADZONE       0.20f     /* stick travel ignored (fraction)                */
#define DPAD_TAKEN     0.15f     /* left stick past this: its D-pad bits are ours  */

static int  s_stick = 0;         /* 0 = left, 1 = right */
static int  s_speed = 5;         /* 1-10 */

#if defined(CYC_WITH_SDL)
static SDL_GameController *s_pad;
static float s_px = -1, s_py = -1;   /* pointer, in game pixels             */
static bool  s_using;                /* the pointer was last moved by the stick */
static bool  s_a_was_down;
static bool  s_a_is_click;           /* this A press is a click, not the NES A */
static SDL_GameControllerButton s_a_button = SDL_CONTROLLER_BUTTON_A;   /* the pad's NES A */

/* Which pad button is the NES A: config.ini [Gamepad.Player1] a = ...
 * (nesrecomp's own binding). Read when the game starts; the start screen is
 * where it's changed. Anything that isn't a plain button (a trigger, a
 * combination) leaves it on the A button. */
static void read_a_binding(void)
{
    s_a_button = SDL_CONTROLLER_BUTTON_A;
    FILE *f = fopen(cyc_settings_default_path(), "r");
    if (!f) return;
    char line[256];
    bool in_pad = false;
    while (fgets(line, sizeof line, f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') ++p;
        if (*p == '[') { in_pad = !strncmp(p, "[Gamepad.Player1]", 17); continue; }
        if (!in_pad || p[0] != 'a' || (p[1] != ' ' && p[1] != '=')) continue;
        char *v = strchr(p, '=');
        if (!v) break;
        ++v;
        while (*v == ' ' || *v == '\t') ++v;
        size_t n = strcspn(v, " \t\r\n");
        v[n] = '\0';
        SDL_GameControllerButton b = SDL_GameControllerGetButtonFromString(v);
        if (b != SDL_CONTROLLER_BUTTON_INVALID) s_a_button = b;
        break;
    }
    fclose(f);
}

static SDL_GameController *find_pad(void)
{
    if (s_pad && SDL_GameControllerGetAttached(s_pad)) return s_pad;
    if (s_pad) { SDL_GameControllerClose(s_pad); s_pad = NULL; }
    for (int i = 0; i < SDL_NumJoysticks(); ++i)
        if (SDL_IsGameController(i) && (s_pad = SDL_GameControllerOpen(i)) != NULL) {
            sg_log("pad pointer: using %s", SDL_GameControllerName(s_pad) ? SDL_GameControllerName(s_pad) : "a game controller");
            return s_pad;
        }
    return NULL;
}

static float axis(SDL_GameController *pad, SDL_GameControllerAxis a)
{
    return (float)SDL_GameControllerGetAxis(pad, a) / 32767.0f;
}

static void stick_xy(SDL_GameController *pad, float *x, float *y)
{
    *x = axis(pad, s_stick ? SDL_CONTROLLER_AXIS_RIGHTX : SDL_CONTROLLER_AXIS_LEFTX);
    *y = axis(pad, s_stick ? SDL_CONTROLLER_AXIS_RIGHTY : SDL_CONTROLLER_AXIS_LEFTY);
}
#endif

static void pad_start(void)
{
#if defined(CYC_WITH_SDL)
    s_px = s_py = -1;
    s_using = false;
    s_a_was_down = s_a_is_click = false;
    read_a_binding();
    sg_log("pad pointer: the %s button clicks; pressing a stick in is A",
           SDL_GameControllerGetStringForButton(s_a_button));
#endif
    sg_log("pad pointer: the %s stick moves the pointer (speed %d)", s_stick ? "right" : "left", s_speed);
}

static void pad_stop(void)
{
#if defined(CYC_WITH_SDL)
    s_using = false;
    s_a_is_click = false;
    if (s_pad) { SDL_GameControllerClose(s_pad); s_pad = NULL; }
#endif
}

/* Before the mouse feature's frame: move the pointer. */
static void pad_frame(void)
{
#if defined(CYC_WITH_SDL)
    if (!sg_feature_mouse.enabled) return;
    SDL_GameController *pad = find_pad();
    if (!pad) return;
    SDL_Window *win = SDL_GetKeyboardFocus();          /* only while the game window has focus */
    SDL_Renderer *ren = win ? SDL_GetRenderer(win) : NULL;
    if (!ren) return;

    float x, y;
    stick_xy(pad, &x, &y);
    float m = sqrtf(x * x + y * y);
    /* While the stick rests, follow the real mouse, so switching between
     * them never makes the pointer jump; start mid-picture if it's outside. */
    if (m <= DEADZONE && sg_pointer_x >= 0) { s_px = sg_pointer_x + 0.5f; s_py = sg_pointer_y + 0.5f; }
    if (s_px < 0) { s_px = 128; s_py = 120; }
    if (m > DEADZONE) {
        /* Gentle near the middle, fast at the edge: speed 5 crosses the
         * picture in about a second at full tilt. */
        float t = (m > 1.0f ? 1.0f : m - DEADZONE) / (1.0f - DEADZONE);
        float pixels = (0.6f + 0.7f * (float)s_speed) * t * t;
        s_px += x / m * pixels;
        s_py += y / m * pixels;
        if (s_px < 0) s_px = 0;
        if (s_px > 255.5f) s_px = 255.5f;
        if (s_py < 0) s_py = 0;
        if (s_py > 239.5f) s_py = 239.5f;
        int wx, wy;
        SDL_RenderLogicalToWindow(ren, s_px, s_py, &wx, &wy);
        SDL_WarpMouseInWindow(win, wx, wy);
        if (!s_using) sg_log("frame %6u  pad pointer: stick moves the pointer", (unsigned)sg_game.frame);
        s_using = true;
    }
#endif
}

/* At the start of the frame, before the game reads its controller (and
 * before the mouse feature adds its presses; it comes later in the list):
 *  - a new A press while the stick is pointing becomes a click at the
 *    pointer, and that A is held back from the game for as long as it's
 *    down. (Checked here, not in pad_frame, which runs at the END of the
 *    frame: by then the game had already acted on the A at its own cursor.)
 *  - the left stick's D-pad directions are taken back out while it moves
 *    the pointer. */
static void pad_input(uint8_t buttons[2])
{
#if defined(CYC_WITH_SDL)
    if (!sg_feature_mouse.enabled || !s_pad) return;
    bool stick_a = !SDL_GameControllerGetButton(s_pad, SDL_CONTROLLER_BUTTON_BACK)
                   && (SDL_GameControllerGetButton(s_pad, SDL_CONTROLLER_BUTTON_LEFTSTICK)
                       || SDL_GameControllerGetButton(s_pad, SDL_CONTROLLER_BUTTON_RIGHTSTICK));
    bool a = stick_a || SDL_GameControllerGetButton(s_pad, s_a_button) != 0;
    if (a && !s_a_was_down && s_using && s_px >= 0) {
        s_a_is_click = true;
        sg_mouse_click_at((int)s_px, (int)s_py);       /* acted on in this frame's mouse_frame */
        sg_log("frame %6u  pad pointer: A clicks at (%d,%d)", (unsigned)sg_game.frame, (int)s_px, (int)s_py);
    }
    if (!a) s_a_is_click = false;
    s_a_was_down = a;
    if (s_a_is_click) buttons[0] &= (uint8_t)~NES_A;
    else if (stick_a) buttons[0] |= NES_A;                /* a stick press is the NES A */
    if (s_stick == 0) {
        float x = axis(s_pad, SDL_CONTROLLER_AXIS_LEFTX), y = axis(s_pad, SDL_CONTROLLER_AXIS_LEFTY);
        const Uint8 *keys = SDL_GetKeyboardState(NULL);
        #define HELD(button, key) (SDL_GameControllerGetButton(s_pad, button) || keys[key])
        if (y < -DPAD_TAKEN && !HELD(SDL_CONTROLLER_BUTTON_DPAD_UP, SDL_SCANCODE_UP))       buttons[0] &= (uint8_t)~NES_UP;
        if (y >  DPAD_TAKEN && !HELD(SDL_CONTROLLER_BUTTON_DPAD_DOWN, SDL_SCANCODE_DOWN))   buttons[0] &= (uint8_t)~NES_DOWN;
        if (x < -DPAD_TAKEN && !HELD(SDL_CONTROLLER_BUTTON_DPAD_LEFT, SDL_SCANCODE_LEFT))   buttons[0] &= (uint8_t)~NES_LEFT;
        if (x >  DPAD_TAKEN && !HELD(SDL_CONTROLLER_BUTTON_DPAD_RIGHT, SDL_SCANCODE_RIGHT)) buttons[0] &= (uint8_t)~NES_RIGHT;
        #undef HELD
    }
    /* A real direction (D-pad or keyboard): A is the NES A again. */
    if ((buttons[0] & 0x0F) && s_using) {
        s_using = false;
        sg_log("frame %6u  pad pointer: direction pressed - A is the NES A again", (unsigned)sg_game.frame);
    }
#else
    (void)buttons;
#endif
}

/* ---- settings ---- */
static bool pad_setting(const char *key, const char *value)
{
    if (sg_same_word(key, "PointerStick")) {
        s_stick = sg_same_word(value, "Right") ? 1 : 0;
        return true;
    }
    if (sg_same_word(key, "StickPressIsA")) return true;   /* an old switch, no longer used: dropped */
    if (sg_same_word(key, "PointerSpeed")) {
        int v = 0;
        for (const char *p = value; *p >= '0' && *p <= '9'; ++p) v = v * 10 + (*p - '0');
        s_speed = v < 1 ? 1 : v > 10 ? 10 : v;
        return true;
    }
    return false;
}

static void pad_save(FILE *f)
{
    fprintf(f, "PointerStick = %s\n", s_stick ? "Right" : "Left");
    fprintf(f, "PointerSpeed = %d\n", s_speed);
}

int  sg_pad_get_stick(void) { return s_stick; }
int  sg_pad_get_speed(void) { return s_speed; }
void sg_pad_set_stick(int stick) { s_stick = stick ? 1 : 0; sg_log("pad pointer: now the %s stick", s_stick ? "right" : "left"); }
void sg_pad_set_speed(int speed) { s_speed = speed < 1 ? 1 : speed > 10 ? 10 : speed; }
void sg_pad_defaults(void) { s_stick = 0; s_speed = 5; }

SgFeature sg_feature_pad = {
    .key = "PadPointer",
    .default_on = true,
    .start = pad_start,
    .stop = pad_stop,
    .frame = pad_frame,
    .input = pad_input,
    .setting = pad_setting,
    .save = pad_save,
};
