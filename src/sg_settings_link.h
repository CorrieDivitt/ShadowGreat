/*
 * sg_settings_link.h - the clickable SETTINGS line on the file select and
 * save screens (opens the Shadowgreat section of the in-game menu).
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Is game pixel (x, y) on the SETTINGS line right now? */
bool sg_settings_link_hit(int x, int y);

/* Open the in-game menu at the Shadowgreat section. */
bool sg_settings_link_open(void);

/* Draw it into the picture (width x height, game picture starting at x0). */
void sg_settings_link_draw(uint32_t *buf, int width, int height, int x0);
