/*
 * feat_narration.c - reads the narration box aloud (Phase 7).
 *
 * Phase 5: when the game starts printing a message (its routine at $C49E),
 * decode the whole message and write it to shadowgate.log. Phase 7 sends
 * the same text to the voice. Narration box only.
 *
 * The hook site is declared in game.toml as "shadowgate.narration.message".
 * It is only switched on while this feature is On.
 */
#include "../sg_features.h"
#include "../sg_game.h"
#include "../sg_text.h"
#include "cyc_mod.h"
#include "mod_function_hooks.h"
#include "mod_runtime.h"

#include <string.h>

#define HOOK_ID   "shadowgate.narration.message"
/* Second way in ($C494): used for automatic story scenes, e.g. the
 * "That pitiful wizard Lakmir" speech on first entering the hall. It
 * skips $C49E's box-clearing and jumps straight into the same print
 * loop, so the message pointer and format are the same. Found 2026-11-08. */
#define HOOK_ID_2 "shadowgate.narration.message-continue"

static char     s_spoken[4096];
static char     s_logged[6144];
static unsigned s_messages;

static int on_message_start(uint16_t addr)
{
    (void)addr;
    CycModRegs r;
    cyc_mod_regs(&r);
    uint16_t msg = (uint16_t)(cyc_mod_peek(SG_RAM_MESSAGE_PTR) |
                              (cyc_mod_peek(SG_RAM_MESSAGE_PTR + 1) << 8));
    if (!sg_text_load_words()) sg_log("narration: could not read the word lists");
    bool ok = sg_text_decode(msg, s_spoken, sizeof s_spoken, s_logged, sizeof s_logged);
    s_messages++;
    sg_log("frame %6u  message #%u at $%04X (A=%02X X=%02X Y=%02X)%s",
           (unsigned)sg_game.frame, s_messages, msg, r.a, r.x, r.y,
           ok ? "" : "  [decode stopped early]");
    sg_log("    %s", s_logged);
    /* Phase 7: hand s_spoken to the voice here. */
    return 0;   /* 0 = let the game print it as normal */
}

NES_MOD_CONSTRUCTOR(sg_register_narration_hook)
{
    nes_mod_register_function_entry_plugin(HOOK_ID, SG_CODE_PRINT_MESSAGE,
                                           on_message_start);
    nes_mod_register_function_entry_plugin(HOOK_ID_2, SG_CODE_PRINT_MESSAGE_2,
                                           on_message_start);
}

static void narration_start(void)
{
    sg_text_forget_words();     /* re-read after each power-on / reset */
    s_messages = 0;
    if (nes_mod_set_function_hook_enabled(HOOK_ID, 1) &&
        nes_mod_set_function_hook_enabled(HOOK_ID_2, 1))
        sg_log("narration: listening for messages");
    else
        sg_log("narration: could not switch on the message hook");
}

SgFeature sg_feature_narration = {
    .key = "Narration",
    .default_on = true,
    .start = narration_start,
};
