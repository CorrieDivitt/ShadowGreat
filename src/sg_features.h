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
#include <stdint.h>

typedef struct SgFeature {
    const char *key;          /* its config.ini [Game] key, e.g. "Mouse"   */
    bool default_on;          /* used when config.ini has no line for it   */
    bool enabled;             /* the live switch (set from config.ini)     */

    /* Optional hooks: leave any of them NULL. Called only while enabled. */
    void (*start)(void);                      /* game powered on / reset   */
    void (*frame)(void);                      /* end of every frame        */
    void (*input)(uint8_t buttons[2]);        /* may add NES button presses */
    void (*event)(const void *sdl_event);     /* raw window events (mouse) */
    /* The picture to show instead of the game's (NULL = the game's). */
    const uint32_t *(*present)(int *width, int *height);
} SgFeature;

extern SgFeature sg_feature_mouse;
extern SgFeature sg_feature_hand;
extern SgFeature sg_feature_narration;
extern SgFeature sg_feature_torch;

/* Where the Windows pointer is, in game pixels (0-255, 0-239), or -1 when
 * it is off the picture. Updated by the mouse feature every frame. */
extern int sg_pointer_x, sg_pointer_y;

/* Append one line to shadowgate.log (beside where the game was started). */
void sg_log(const char *fmt, ...);
