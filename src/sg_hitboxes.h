/*
 * sg_hitboxes.h - the clickable areas of each screen, in game pixels
 * (0-255 across, 0-239 down). The table itself is generated from
 * shadowgate-map.toml (tools/gen_hitboxes.py).
 */
#pragma once
#include <stdint.h>
#include "sg_game.h"

typedef struct SgHitbox {
    SgScreen    screen;
    const char *name;             /* e.g. "LOOK", "SLOT_3", "YES" */
    short       left, top, right, bottom;   /* edges included */
} SgHitbox;

extern const SgHitbox sg_hitboxes[];
extern const int      sg_hitbox_count;

/* The name of the box at game pixel (x, y) on `screen`, or NULL. */
const char *sg_hitbox_at(SgScreen screen, int x, int y);

/* What a click on a play-screen button writes (from [buttons.play]). */
typedef struct SgButton {
    const char *name;
    uint8_t     x, y;      /* cursor position: $AB/$AC and $76/$77 */
    short       ad;        /* $AD, or -1 to leave it alone */
    uint8_t     af;        /* $AF */
} SgButton;
extern const SgButton sg_buttons[];
extern const int      sg_button_count;

/* A menu choice (from [screens.*]): write `value` to `byte`. */
typedef struct SgChoice {
    SgScreen    screen;
    const char *name;
    uint16_t    byte;
    uint8_t     value;
} SgChoice;
extern const SgChoice sg_choices[];
extern const int      sg_choice_count;
