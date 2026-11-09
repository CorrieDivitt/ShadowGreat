/*
 * feat_narration.c - reads the narration box aloud (Phase 7).
 *
 * When the game starts printing a message (its routine at $C49E, or $C494
 * for story scenes), decode the whole message, write it to shadowgate.log
 * and hand it to the voice (sg_voice.c). Narration box only.
 *
 * config.ini [Game] lines:
 *   Narration        = On             the whole feature (voice + log)
 *   NarratorVoice    = Dungeon Master Dungeon Master, Old Man or Woman (or 1-3)
 *   VoiceVolume      = 100            0-100
 *   MusicWhileSpeaking = 40           0-100: game sound level while the voice talks
 *   QuotePause       = 0.8            seconds of quiet after a closing quote
 *   <Narrator>Speed / <Narrator>Depth / <Narrator>Pause   each narrator's own
 *                    voice, e.g. DungeonMasterSpeed = 1.22, OldManDepth = 0.95,
 *                    WomanPause = 0.6 (speed and depth 0.5-1.5, pause 0-2;
 *                    missing = that narrator's normal voice)
 *   ExclaimPause     = 0.6            seconds of quiet after "!"
 *   VoiceFolder      = voice          the voice package: sherpa-onnx DLLs (beside the .exe, or a full path)
 *   VoiceModelFolder =                the Kokoro model; empty = VoiceFolder/kokoro-multi-lang-v1_0
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

/* Defaults (also what "Restore defaults" in the menu goes back to) */
#define DEFAULT_PRESET        0      /* Dungeon Master */
#define DEFAULT_VOLUME        100
#define DEFAULT_MUSIC         40
#define DEFAULT_QUOTE_PAUSE   0.8f
#define DEFAULT_EXCLAIM_PAUSE 0.6f

static int  s_preset = DEFAULT_PRESET;      /* index into sg_voice_presets */
static int  s_volume = DEFAULT_VOLUME;
static int  s_music = DEFAULT_MUSIC;
static float s_quote_pause = DEFAULT_QUOTE_PAUSE;
static float s_exclaim_pause = DEFAULT_EXCLAIM_PAUSE;

/* Each narrator's own speed, depth and pause, in hundredths (Kokoro speed,
 * playback pitch factor and silence scale). Start as the preset's values. */
#define MAX_NARRATORS 8
enum { TUNE_SPEED, TUNE_DEPTH, TUNE_PAUSE, TUNE_COUNT };
static const char *const k_tune_words[TUNE_COUNT] = { "Speed", "Depth", "Pause" };
static int  s_tune[MAX_NARRATORS][TUNE_COUNT];
static bool s_tune_ready;

static int hundredths(float v) { return (int)(v * 100.0f + (v < 0 ? -0.5f : 0.5f)); }

static int tune_clamp(int which, int v)
{
    const int lo = which == TUNE_PAUSE ? 0 : 50, hi = which == TUNE_PAUSE ? 200 : 150;
    return v < lo ? lo : v > hi ? hi : v;
}

static void tune_reset(void)
{
    for (int i = 0; i < sg_voice_preset_count && i < MAX_NARRATORS; ++i) {
        s_tune[i][TUNE_SPEED] = hundredths(sg_voice_presets[i].speed);
        s_tune[i][TUNE_DEPTH] = hundredths(sg_voice_presets[i].pitch);
        s_tune[i][TUNE_PAUSE] = hundredths(sg_voice_presets[i].pause);
    }
    s_tune_ready = true;
}

static void tune_init(void) { if (!s_tune_ready) tune_reset(); }

/* "Dungeon Master" + "Speed" -> "DungeonMasterSpeed" */
static void tune_key(int narrator, int which, char *out, size_t size)
{
    size_t o = 0;
    for (const char *p = sg_voice_presets[narrator].name; *p && o + 1 < size; ++p)
        if (*p != ' ') out[o++] = *p;
    out[o] = '\0';
    snprintf(out + o, size - o, "%s", k_tune_words[which]);
}

/* Give the voice the current narrator's own values. */
static void tune_apply(void)
{
    tune_init();
    if (s_preset < 0 || s_preset >= MAX_NARRATORS) return;
    sg_voice_set_tuning(s_tune[s_preset][TUNE_SPEED] / 100.0f,
                        s_tune[s_preset][TUNE_DEPTH] / 100.0f,
                        s_tune[s_preset][TUNE_PAUSE] / 100.0f);
}

/* A message is only spoken once the narration box is really on screen
 * ($E5 = $6C, shadowgate-map.toml). Things like the inventory headings
 * ("GOODS", "TORCH") go through the same printer without the box. */
#define SG_RAM_SCREEN_ID_BOX 0x6C
#define BOX_WAIT_FRAMES      20
static bool     s_waiting_for_box;
static uint32_t s_message_frame;
static char s_voice_dir[1024] = "voice";
static char s_model_dir[1024] = "";

static int on_message_start(uint16_t addr)
{
    (void)addr;
    if (!sg_feature_narration.enabled) return 0;   /* switched Off in the menu */
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
    sg_voice_say(s_spoken);              /* made now, played once the box shows */
    s_waiting_for_box = true;
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
            sg_voice_release();
        } else if (sg_game.frame - s_message_frame > BOX_WAIT_FRAMES) {
            s_waiting_for_box = false;
            sg_voice_stop();
            sg_log("narration: no message box for message #%u - not spoken", s_messages);
        }
    }
    if (sg_game.screen != s_last_screen) {
        /* (not while a new message waits for its box: that one is
         * already handled by the wait above) */
        if (s_last_screen == SG_SCREEN_MESSAGE && !s_waiting_for_box) sg_voice_stop();
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
    sg_voice_set_pauses(s_quote_pause, s_exclaim_pause);
    sg_voice_start(s_preset, s_voice_dir, s_model_dir, s_volume, s_music);
    tune_apply();
    s_last_screen = SG_SCREEN_UNKNOWN;
    if (nes_mod_set_function_hook_enabled(HOOK_ID, 1) &&
        nes_mod_set_function_hook_enabled(HOOK_ID_2, 1) &&
        nes_mod_set_function_hook_enabled(HOOK_TEXT_A, 1) &&
        nes_mod_set_function_hook_enabled(HOOK_PAGE_A, 1))
        sg_log("narration: listening for messages");
    else
        sg_log("narration: could not switch on the message hook");
}

static void narration_stop(void)
{
    s_waiting_for_box = false;
    sg_voice_stop();
}

int sg_narration_get(SgNarrSetting which)
{
    switch (which) {
    case SG_NARR_PRESET:         return s_preset;
    case SG_NARR_VOLUME:         return s_volume;
    case SG_NARR_MUSIC:          return s_music;
    case SG_NARR_QUOTE_TENTHS:   return (int)(s_quote_pause * 10.0f + 0.5f);
    case SG_NARR_EXCLAIM_TENTHS: return (int)(s_exclaim_pause * 10.0f + 0.5f);
    case SG_NARR_SPEED: tune_init(); return s_tune[s_preset][TUNE_SPEED];
    case SG_NARR_DEPTH: tune_init(); return s_tune[s_preset][TUNE_DEPTH];
    case SG_NARR_PAUSE: tune_init(); return s_tune[s_preset][TUNE_PAUSE];
    }
    return 0;
}

void sg_narration_set(SgNarrSetting which, int v)
{
    switch (which) {
    case SG_NARR_PRESET:
        if (v >= 0 && v < sg_voice_preset_count && v < MAX_NARRATORS) {
            s_preset = v;
            sg_voice_set_preset(v);
            tune_apply();               /* this narrator's own speed, depth and pause */
        }
        break;
    case SG_NARR_VOLUME: s_volume = v < 0 ? 0 : v > 100 ? 100 : v; sg_voice_set_volume(s_volume, s_music); break;
    case SG_NARR_MUSIC:  s_music  = v < 0 ? 0 : v > 100 ? 100 : v; sg_voice_set_volume(s_volume, s_music); break;
    case SG_NARR_QUOTE_TENTHS:   s_quote_pause = v / 10.0f;   sg_voice_set_pauses(s_quote_pause, s_exclaim_pause); break;
    case SG_NARR_EXCLAIM_TENTHS: s_exclaim_pause = v / 10.0f; sg_voice_set_pauses(s_quote_pause, s_exclaim_pause); break;
    case SG_NARR_SPEED: tune_init(); s_tune[s_preset][TUNE_SPEED] = tune_clamp(TUNE_SPEED, v); tune_apply(); break;
    case SG_NARR_DEPTH: tune_init(); s_tune[s_preset][TUNE_DEPTH] = tune_clamp(TUNE_DEPTH, v); tune_apply(); break;
    case SG_NARR_PAUSE: tune_init(); s_tune[s_preset][TUNE_PAUSE] = tune_clamp(TUNE_PAUSE, v); tune_apply(); break;
    }
}

void sg_narration_narrator_defaults(void)
{
    tune_init();                         /* this narrator's own voice back to normal */
    if (s_preset >= 0 && s_preset < sg_voice_preset_count && s_preset < MAX_NARRATORS) {
        s_tune[s_preset][TUNE_SPEED] = hundredths(sg_voice_presets[s_preset].speed);
        s_tune[s_preset][TUNE_DEPTH] = hundredths(sg_voice_presets[s_preset].pitch);
        s_tune[s_preset][TUNE_PAUSE] = hundredths(sg_voice_presets[s_preset].pause);
    }
    tune_apply();
    s_quote_pause = DEFAULT_QUOTE_PAUSE;
    s_exclaim_pause = DEFAULT_EXCLAIM_PAUSE;
    sg_voice_set_pauses(s_quote_pause, s_exclaim_pause);
}

void sg_narration_defaults(void)
{
    tune_reset();                        /* every narrator's own voice back to normal */
    sg_narration_set(SG_NARR_PRESET, DEFAULT_PRESET);
    sg_narration_narrator_defaults();
    sg_narration_set(SG_NARR_VOLUME, DEFAULT_VOLUME);
    sg_narration_set(SG_NARR_MUSIC, DEFAULT_MUSIC);
}

static bool narration_setting(const char *key, const char *value)
{
    tune_init();
    for (int i = 0; i < sg_voice_preset_count && i < MAX_NARRATORS; ++i)
        for (int w = 0; w < TUNE_COUNT; ++w) {
            char name[64];
            tune_key(i, w, name, sizeof name);
            if (sg_same_word(key, name)) {
                s_tune[i][w] = tune_clamp(w, hundredths((float)atof(value)));
                return true;
            }
        }
    if (sg_same_word(key, "NarratorVoice")) {
        for (int i = 0; i < sg_voice_preset_count; ++i)
            if (sg_same_word(value, sg_voice_presets[i].name)) s_preset = i;
        int n = atoi(value);                  /* 1, 2 or 3 also works */
        if (n >= 1 && n <= sg_voice_preset_count) s_preset = n - 1;
        /* The voices' names before 2026-11-08: Narrator and Plain were
         * Dungeon Master's voice; Grim was the other deep man, now Old Man. */
        if (sg_same_word(value, "Narrator") || sg_same_word(value, "Plain")) s_preset = 0;
        if (sg_same_word(value, "Grim")) s_preset = 1;
        return true;
    }
    if (sg_same_word(key, "VoiceVolume")) {
        int v = atoi(value);
        s_volume = v < 0 ? 0 : v > 100 ? 100 : v;
        return true;
    }
    if (sg_same_word(key, "QuotePause"))   { s_quote_pause = (float)atof(value); return true; }
    if (sg_same_word(key, "ExclaimPause")) { s_exclaim_pause = (float)atof(value); return true; }
    if (sg_same_word(key, "MusicWhileSpeaking")) {
        int v = atoi(value);
        s_music = v < 0 ? 0 : v > 100 ? 100 : v;
        return true;
    }
    if (sg_same_word(key, "VoiceFolder")) {
        snprintf(s_voice_dir, sizeof s_voice_dir, "%s", value);
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
    fprintf(f, "QuotePause = %.2f\n", s_quote_pause);
    fprintf(f, "ExclaimPause = %.2f\n", s_exclaim_pause);
    tune_init();
    for (int i = 0; i < sg_voice_preset_count && i < MAX_NARRATORS; ++i)
        for (int w = 0; w < TUNE_COUNT; ++w) {
            char name[64];
            tune_key(i, w, name, sizeof name);
            fprintf(f, "%s = %.2f\n", name, s_tune[i][w] / 100.0f);
        }
    fprintf(f, "VoiceFolder = %s\n", s_voice_dir);
    fprintf(f, "VoiceModelFolder = %s\n", s_model_dir);
}

SgFeature sg_feature_narration = {
    .key = "Narration",
    .default_on = true,
    .start = narration_start,
    .stop = narration_stop,
    .frame = narration_frame,
    .setting = narration_setting,
    .save = narration_save,
    .audio = sg_voice_mix,
};
