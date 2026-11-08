/*
 * feat_mouse.c - point and click (Phase 6).
 *
 * Empty for now: Phase 5 only proves the On/Off switch works. Phase 6 fills
 * in event() (read the Windows mouse), input() (press A on a click) and
 * frame() (write cursor / selection bytes from shadowgate-map.toml).
 */
#include "../sg_features.h"

static void mouse_start(void)
{
    sg_log("mouse: ready (does nothing yet)");
}

SgFeature sg_feature_mouse = {
    .key = "Mouse",
    .default_on = true,
    .start = mouse_start,
};
