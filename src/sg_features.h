/*
 * sg_features.h - how Shadowgate's add-on features plug in.
 *
 * Every add-on (mouse, narration, torch tracker, and any later one) is a
 * "feature": one C file in src/features/ that fills in one SgFeature.
 * sg_host.c keeps the list, reads each feature's On/Off switch from
 * config.ini [Game], and only calls a feature's functions while it is On.
 * A feature that is Off never runs, so the game plays exactly as stock.
 *
 * To add a new feature:
 *   1. copy src/features/feat_torch.c to a new file and rename things;
 *   2. add its "extern SgFeature ..." line below;
 *   3. add it to the list in sg_host.c and to HOST_EXTRAS in CMakeLists.txt.
 *
 * Rule from shadowgate-map.toml: a feature only ever writes values that are
 * listed in that file.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct SgFeature {
    const char *key;          /* its config.ini [Game] key, e.g. "Mouse"   */
    bool default_on;          /* used when config.ini has no line for it   */
    bool enabled;             /* the live switch (set from config.ini)     */

    /* Optional hooks: leave any of them NULL. Called only while enabled. */
    void (*start)(void);                      /* game powered on / reset, or switched On mid-game */
    void (*stop)(void);                       /* switched Off mid-game: tidy up */
    void (*frame)(void);                      /* end of every frame        */
    void (*input)(uint8_t buttons[2]);        /* may add NES button presses */
    void (*event)(const void *sdl_event);     /* raw window events (mouse) */
    /* Draw on top of the game's picture (a copy; width x height, the game's
     * 256-pixel picture starting at column x0). Called in feature order, so
     * later features draw on top (the hand comes last). */
    void (*draw)(uint32_t *buf, int width, int height, int x0);
    /* Extra config.ini [Game] lines of its own (e.g. NarratorVoice):
     * setting() returns true if the key was one of them; save() writes
     * them back. Called whether the feature is On or Off. */
    bool (*setting)(const char *key, const char *value);
    void (*save)(FILE *f);
    /* Mix something into the game's sound (mono, `rate` Hz). */
    void (*audio)(int16_t *pcm, size_t count, unsigned rate);
} SgFeature;

extern SgFeature sg_feature_mouse;
extern SgFeature sg_feature_hand;
extern SgFeature sg_feature_narration;
extern SgFeature sg_feature_torch;
extern SgFeature sg_feature_pad;

/* A left click at a game pixel, as if from the mouse (feat_mouse.c). */
void sg_mouse_click_at(int x, int y);

/* The pad pointer's own settings (feat_pad.c), for the in-game menu. */
int  sg_pad_get_stick(void);       /* 0 = left stick, 1 = right stick */
int  sg_pad_get_speed(void);       /* 1-10 */
void sg_pad_set_stick(int stick);
void sg_pad_set_speed(int speed);
void sg_pad_defaults(void);

/* Where the Windows pointer is, in game pixels (0-255, 0-239), or -1 when
 * it is off the picture. Updated by the mouse feature every frame. */
extern int sg_pointer_x, sg_pointer_y;

/* The game's two "is A pressed?" checks in the message box ($C7F4 while
 * printing, $C9F9 at a page end). Both features use them, so the hook
 * lives in feat_mouse.c and tells the narration feature too. */
#ifndef SG_SITE_TEXT_A
#ifdef SITE_TEXT_A
#define SG_SITE_TEXT_A SITE_TEXT_A
#else
#define SG_SITE_TEXT_A 0xC7F4
#endif
#endif

#ifndef SG_SITE_PAGE_A
#ifdef SITE_PAGE_A
#define SG_SITE_PAGE_A SITE_PAGE_A
#else
#define SG_SITE_PAGE_A 0xC9F9
#endif
#endif
void sg_narration_print_site(uint16_t addr);

#if defined(CYC_WITH_RECOMP_UI)
bool cyc_ui_menu_open(void);
#else
static inline bool cyc_ui_menu_open(void) { return false; }
#endif

/* Switch a feature On or Off while the game runs (the in-game menu). */
void sg_feature_switch(SgFeature *f, bool on);

/* The narration feature's own settings, for the in-game menu. */
typedef enum SgNarrSetting {
    SG_NARR_PRESET,          /* index into sg_voice_presets            */
    SG_NARR_VOLUME,          /* 0-100                                  */
    SG_NARR_MUSIC,           /* 0-100: game sound while the voice talks */
    SG_NARR_QUOTE_TENTHS,    /* pause after quotes, tenths of a second */
    SG_NARR_EXCLAIM_TENTHS,  /* pause after "!", tenths of a second    */
    SG_NARR_SPEED,           /* this narrator's speed, hundredths (50-150) */
    SG_NARR_DEPTH,           /* this narrator's depth, hundredths (50-150) */
    SG_NARR_PAUSE,           /* this narrator's pause, hundredths (0-200)  */
} SgNarrSetting;
int  sg_narration_get(SgNarrSetting which);
void sg_narration_set(SgNarrSetting which, int value);
void sg_narration_defaults(void);   /* every narration setting back to normal */
/* The chosen narrator's speed, depth and pause, and the two pauses after
 * quotes and "!", back to normal. The narrator itself stays as chosen. */
void sg_narration_narrator_defaults(void);

/* The in-game menu rows (sg_menu.c; only in builds with recomp-ui). */
struct CycHostExtras;
void sg_menu_attach(struct CycHostExtras *extras);

/* Case-blind comparison for config.ini keys and values. */
bool sg_same_word(const char *a, const char *b);

/* Append one line to shadowgate.log (beside where the game was started). */
void sg_log(const char *fmt, ...);
