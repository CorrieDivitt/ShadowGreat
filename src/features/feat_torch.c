/*
 * feat_torch.c - shows how many turns the lit torch has left (Phase 10).
 *
 * Empty for now; Off unless config.ini says "TorchTracker = On".
 * Also the template to copy when adding a new feature.
 */
#include "../sg_features.h"

static void torch_start(void)
{
    sg_log("torch tracker: ready (does nothing yet)");
}

SgFeature sg_feature_torch = {
    .key = "TorchTracker",
    .default_on = false,
    .start = torch_start,
};
