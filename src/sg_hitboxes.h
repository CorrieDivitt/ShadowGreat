/*
 * sg_hitboxes.h - the clickable areas of each screen, in game pixels
 * (0-255 across, 0-239 down). The table itself is generated from
 * shadowgate-map.toml (tools/gen_hitboxes.py).
 */
#pragma once
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
