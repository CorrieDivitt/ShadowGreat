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

/* A choosable voice. Chosen in config.ini: NarratorVoice = Narrator */
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

/* false when this build was made without sherpa-onnx (then all of the
 * functions below do nothing). */
bool sg_voice_built_in(void);

/* Start the voice thread (once) and load the model in the background.
 * model_dir: the kokoro-multi-lang-v1_0 folder (relative = beside the .exe).
 * volume: 0-100. music: 0-100, how loud the game's own sound stays while
 * the voice is talking. Calling it again (game reset) just stops speech. */
void sg_voice_start(int preset, const char *model_dir, int volume, int music);

/* Say this, cutting off anything still being said. '\f' marks a page
 * end: the voice stops there until sg_voice_page() lets it go on. */
void sg_voice_say(const char *text);

/* The game is now showing page `page` (0 = first) of the message. The
 * voice goes on to that page, skipping whatever of the earlier pages it
 * had not said yet, so it stays in step with the box. */
void sg_voice_page(int page);

/* Stop talking. */
void sg_voice_stop(void);

/* Add the voice to one batch of the game's sound (mono, `rate` Hz).
 * Called on the emulation thread, once per batch. */
void sg_voice_mix(int16_t *pcm, size_t count, unsigned rate);
