/*
 * feat_narration.c - reads the narration box aloud (Phase 7).
 *
 * When the game starts printing a message (its routine at $C49E, or $C494
 * for story scenes), decode the whole message, write it to shadowgate.log
 * and hand it to the voice (sg_voice.c). Narration box only.
 *
 * config.ini [Game] lines:
 *   Narration        = On             the whole feature (voice + log)
 *   NarratorVoice    = Narrator       Narrator, Grim or Plain
 *   VoiceVolume      = 100            0-100
 *   MusicWhileSpeaking = 40           0-100: game sound level while the voice talks
 *   VoiceModelFolder = kokoro-multi-lang-v1_0   (beside the .exe, or a full path)
 *
 * The hook site is declared in game.toml as "shadowgate.narration.message".
 * It is only switched on while this feature is On.
 */
#include "../sg_features.h"
#include "../sg_game.h"
#include "../sg_text.h"
#include "../sg_voice.h"
#include "cyc_mod.h"
#include "mod_function_hooks.h"
#include "mod_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Where the Kokoro model folder is, unless config.ini says otherwise
 * (set by CMakeLists.txt: SHADOWGATE_VOICE_MODEL). */
#ifndef SG_DEFAULT_VOICE_MODEL
#define SG_DEFAULT_VOICE_MODEL "kokoro-multi-lang-v1_0"
#endif

#define HOOK_ID   "shadowgate.narration.message"
/* Second way in ($C494): used for automatic story scenes, e.g. the
 * "That pitiful wizard Lakmir" speech on first entering the hall. It
 * skips $C49E's box-clearing and jumps straight into the same print
 * loop, so the message pointer and format are the same. Found 2026-11-08. */
#define HOOK_ID_2 "shadowgate.narration.message-continue"

/* The inventory heading ("GOODS", "SPELL", "KEY 1") is drawn by the same
 * printer from this one message; it is not the narration box. */
#define SG_MSG_INVENTORY_HEADING 0xBE08

/* The A-check hooks that tell us when the player turns a page. They are
 * registered in feat_mouse.c; we only switch them on here as well. */
#define HOOK_TEXT_A "shadowgate.mouse.text-a"
#define HOOK_PAGE_A "shadowgate.mouse.page-a"

static char     s_spoken[4096];
static char     s_logged[6144];
static unsigned s_messages;

/* settings */
static int      s_page;               /* page of the message being shown */
static bool     s_at_page_end;        /* the game is waiting for A        */
static SgScreen s_last_screen;

static int  s_preset;                       /* index into sg_voice_presets */
static int  s_volume = 100;
static int  s_music = 40;

/* A message is only spoken once the narration box is really on screen
 * ($E5 = $6C, shadowgate-map.toml). Things like the inventory headings
 * ("GOODS", "TORCH") go through the same printer without the box. */
#define SG_RAM_SCREEN_ID_BOX 0x6C
#define BOX_WAIT_FRAMES      20
static bool     s_waiting_for_box;
static uint32_t s_message_frame;
static char s_model_dir[1024] = SG_DEFAULT_VOICE_MODEL;

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
    s_page = 0;
    s_at_page_end = false;
    s_waiting_for_box = false;
    if ((msg & 0xFF00) == (SG_MSG_INVENTORY_HEADING & 0xFF00)) return 0;  /* inventory headings */
    s_waiting_for_box = true;            /* spoken from narration_frame, once the box shows */
    s_message_frame = sg_game.frame;
    return 0;   /* 0 = let the game print it as normal */
}

/* Called from feat_mouse.c's hook each time the game checks for A:
 * at $C9F9 it is waiting at a page end; the next $C7F4 means the player
 * pressed A and the next page is printing, so the voice may go on. */
void sg_narration_print_site(uint16_t addr)
{
    if (addr == SG_SITE_PAGE_A) {
        s_at_page_end = true;
    } else if (addr == SG_SITE_TEXT_A && s_at_page_end) {
        s_at_page_end = false;
        s_page++;
        sg_voice_page(s_page);
    }
}

/* The message box closed: stop talking. */
static void narration_frame(void)
{
    if (s_waiting_for_box) {
        if (cyc_mod_peek(SG_RAM_SCREEN_ID) == SG_RAM_SCREEN_ID_BOX) {
            s_waiting_for_box = false;
            sg_voice_say(s_spoken);
            if (s_page > 0) sg_voice_page(s_page);   /* the player was quick */
        } else if (sg_game.frame - s_message_frame > BOX_WAIT_FRAMES) {
            s_waiting_for_box = false;
            sg_log("narration: no message box for message #%u - not spoken", s_messages);
        }
    }
    if (sg_game.screen != s_last_screen) {
        if (s_last_screen == SG_SCREEN_MESSAGE) sg_voice_stop();
        s_last_screen = sg_game.screen;
    }
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
    s_waiting_for_box = false;
    sg_voice_start(s_preset, s_model_dir, s_volume, s_music);
    s_last_screen = SG_SCREEN_UNKNOWN;
    if (nes_mod_set_function_hook_enabled(HOOK_ID, 1) &&
        nes_mod_set_function_hook_enabled(HOOK_ID_2, 1) &&
        nes_mod_set_function_hook_enabled(HOOK_TEXT_A, 1) &&
        nes_mod_set_function_hook_enabled(HOOK_PAGE_A, 1))
        sg_log("narration: listening for messages");
    else
        sg_log("narration: could not switch on the message hook");
}

static bool narration_setting(const char *key, const char *value)
{
    if (sg_same_word(key, "NarratorVoice")) {
        for (int i = 0; i < sg_voice_preset_count; ++i)
            if (sg_same_word(value, sg_voice_presets[i].name)) s_preset = i;
        int n = atoi(value);                  /* 1, 2 or 3 also works */
        if (n >= 1 && n <= sg_voice_preset_count) s_preset = n - 1;
        return true;
    }
    if (sg_same_word(key, "VoiceVolume")) {
        int v = atoi(value);
        s_volume = v < 0 ? 0 : v > 100 ? 100 : v;
        return true;
    }
    if (sg_same_word(key, "MusicWhileSpeaking")) {
        int v = atoi(value);
        s_music = v < 0 ? 0 : v > 100 ? 100 : v;
        return true;
    }
    if (sg_same_word(key, "VoiceModelFolder")) {
        snprintf(s_model_dir, sizeof s_model_dir, "%s", value);
        return true;
    }
    return false;
}

static void narration_save(FILE *f)
{
    fprintf(f, "NarratorVoice = %s\n", sg_voice_presets[s_preset].name);
    fprintf(f, "VoiceVolume = %d\n", s_volume);
    fprintf(f, "MusicWhileSpeaking = %d\n", s_music);
    fprintf(f, "VoiceModelFolder = %s\n", s_model_dir);
}

SgFeature sg_feature_narration = {
    .key = "Narration",
    .default_on = true,
    .start = narration_start,
    .frame = narration_frame,
    .setting = narration_setting,
    .save = narration_save,
    .audio = sg_voice_mix,
};
