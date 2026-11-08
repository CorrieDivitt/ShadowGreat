/*
 * feat_narration.c - reads the narration box aloud (Phase 7).
 *
 * Empty for now. Phase 5's next task adds the text hook (log each message
 * as it starts printing); Phase 7 sends that text to Kokoro, with the
 * Windows voice as backup. Narration box only.
 */
#include "../sg_features.h"

static void narration_start(void)
{
    sg_log("narration: ready (does nothing yet)");
}

SgFeature sg_feature_narration = {
    .key = "Narration",
    .default_on = true,
    .start = narration_start,
};
