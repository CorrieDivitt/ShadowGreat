/*
 * sg_voice.h - the narrator's voice (Phase 7).
 *
 * Speaks the narration box with Kokoro (an open text-to-speech model) run
 * by sherpa-onnx, both downloaded separately by the player; neither is in
 * this project. The words come from the running game (sg_text.c), so no
 * game text is stored anywhere.
 *
 * All the slow work (loading the model, making speech) happens on a
 * thread of its own, so the game never waits for the voice.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A choosable voice. Chosen in config.ini: NarratorVoice = Dungeon Master */
typedef struct SgVoicePreset {
    const char *name;
    int   sid;          /* Kokoro voice number (27 = bm_lewis)                  */
    int   blend_sid;    /* second voice mixed in, or -1                         */
    float blend;        /* how much of the second voice (0.4 = 40 %)           */
    float speed;        /* Kokoro speaking speed                                */
    float pitch;        /* playback factor: below 1 = deeper (and a bit slower) */
    float pause;        /* Kokoro silence scale: pauses at commas and full stops */
} SgVoicePreset;

extern const SgVoicePreset sg_voice_presets[];
extern const int sg_voice_preset_count;

/* false when the voice can't be used: the build has no window, or the
 * voice package (sherpa-onnx DLL + Kokoro model) is not installed. Then the
 * functions below do nothing. */
bool sg_voice_built_in(void);

/* Start the voice thread (once) and load the voice in the background.
 * voice_dir: the voice package folder holding sherpa-onnx's DLLs ("voice"
 * beside the .exe by default). model_dir: the kokoro-multi-lang-v1_0 folder,
 * or NULL / "" for voice_dir/kokoro-multi-lang-v1_0. Relative = beside the .exe.
 * volume: 0-100. music: 0-100, how loud the game's own sound stays while
 * the voice is talking. Calling it again (game reset) just stops speech. */
void sg_voice_start(int preset, const char *voice_dir, const char *model_dir, int volume, int music);

/* Get ready to say this, cutting off anything still being said. The
 * speech is made at once but stays silent until sg_voice_release().
 * '\f' marks a page end: the voice stops there until sg_voice_page()
 * lets it go on. */
void sg_voice_say(const char *text);

/* Live changes from the in-game menu. A new preset loads in the background
 * (about 1.5 s) and is used from the next message. */
void sg_voice_set_preset(int preset);
void sg_voice_set_volume(int volume, int music);
/* The narrator's own speed, depth and pause, replacing the preset's speed,
 * pitch and pause (call again after sg_voice_set_preset). From the next
 * message; depth at once. */
void sg_voice_set_tuning(float speed, float depth, float pause);

/* Extra pauses, in seconds as heard: after the closing quote of spoken
 * dialogue, and after an exclamation mark. Call before sg_voice_start(). */
void sg_voice_set_pauses(float quote_seconds, float exclaim_seconds);

/* The message box is on screen: start playing what sg_voice_say() got. */
void sg_voice_release(void);

/* The game is now showing page `page` (0 = first) of the message. The
 * voice goes on to that page, skipping whatever of the earlier pages it
 * had not said yet, so it stays in step with the box. */
void sg_voice_page(int page);

/* Stop talking. */
void sg_voice_stop(void);

/* Add the voice to one batch of the game's sound (mono, `rate` Hz).
 * Called on the emulation thread, once per batch. */
void sg_voice_mix(int16_t *pcm, size_t count, unsigned rate);
