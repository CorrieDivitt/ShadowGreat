/*
 * sg_host.c - the one place nesrecomp calls into our code.
 *
 * nesrecomp asks for cyc_host_extras() at start. We hand it a table of
 * functions; each of them simply passes the call on to every feature that
 * is switched On. Settings live in config.ini (beside the .exe) under
 * [Game], one line per feature:
 *
 *     [Game]
 *     Mouse = On
 *     Narration = On
 *     TorchTracker = Off
 *     NarratorVoice = Narrator      (a feature's own extra lines)
 *
 * The launcher (Phase 8) will edit these same lines.
 */
#include "cyc_host_extras.h"
#include "sg_features.h"
#include "sg_game.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ---- the feature list: add new features here ---- */
static SgFeature *const s_features[] = {
    &sg_feature_mouse,
    &sg_feature_hand,       /* after mouse: uses its pointer position */
    &sg_feature_narration,
    &sg_feature_torch,
};
#define SG_FEATURE_COUNT (sizeof(s_features) / sizeof(s_features[0]))

/* ---- log file ---- */
void sg_log(const char *fmt, ...)
{
    FILE *f = fopen("shadowgate.log", "a");
    if (!f) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

/* ---- settings ---- */
static bool s_settings_seen[SG_FEATURE_COUNT];

bool sg_same_word(const char *a, const char *b)
{
    for (; *a && *b; ++a, ++b) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
        if (x != y) return false;
    }
    return *a == *b;
}

static bool parse_on(const char *value, bool fallback)
{
    if (sg_same_word(value, "on") || sg_same_word(value, "true") ||
        sg_same_word(value, "yes") || sg_same_word(value, "1")) return true;
    if (sg_same_word(value, "off") || sg_same_word(value, "false") ||
        sg_same_word(value, "no") || sg_same_word(value, "0")) return false;
    return fallback;   /* anything else: keep the default */
}

static void load_setting(void *ctx, const char *key, const char *value)
{
    (void)ctx;
    for (size_t i = 0; i < SG_FEATURE_COUNT; ++i) {
        SgFeature *f = s_features[i];
        if (sg_same_word(key, f->key)) {
            f->enabled = parse_on(value, f->default_on);
            s_settings_seen[i] = true;
            return;
        }
    }
    for (size_t i = 0; i < SG_FEATURE_COUNT; ++i)
        if (s_features[i]->setting && s_features[i]->setting(key, value)) return;
}

static void save_settings(void *ctx, FILE *out)
{
    (void)ctx;
    for (size_t i = 0; i < SG_FEATURE_COUNT; ++i)
        fprintf(out, "%s = %s\n", s_features[i]->key,
                s_features[i]->enabled ? "On" : "Off");
    for (size_t i = 0; i < SG_FEATURE_COUNT; ++i)
        if (s_features[i]->save) s_features[i]->save(out);
}

/* ---- passing calls on to the features that are On ---- */
static void power_on(void *ctx)
{
    (void)ctx;
    sg_log("--- Shadowgate started ---");
    sg_game_reset();
    for (size_t i = 0; i < SG_FEATURE_COUNT; ++i) {
        SgFeature *f = s_features[i];
        sg_log("%-13s %s%s", f->key, f->enabled ? "On" : "Off",
               s_settings_seen[i] ? "" : "  (default)");
        if (f->enabled && f->start) f->start();
    }
}

static bool any_feature_on(void)
{
    for (size_t i = 0; i < SG_FEATURE_COUNT; ++i)
        if (s_features[i]->enabled) return true;
    return false;
}

static void frame_end(void *ctx)
{
    (void)ctx;
    /* With every feature Off we don't even read the game's memory. */
    if (!any_feature_on()) return;
    sg_game_update();   /* refresh sg_game (screen etc.) for the features */
    for (size_t i = 0; i < SG_FEATURE_COUNT; ++i)
        if (s_features[i]->enabled && s_features[i]->frame) s_features[i]->frame();
}

static void input(void *ctx, uint8_t buttons[2])
{
    (void)ctx;
    for (size_t i = 0; i < SG_FEATURE_COUNT; ++i)
        if (s_features[i]->enabled && s_features[i]->input) s_features[i]->input(buttons);
}

static void event(void *ctx, const void *sdl_event, int player)
{
    (void)ctx; (void)player;
    for (size_t i = 0; i < SG_FEATURE_COUNT; ++i)
        if (s_features[i]->enabled && s_features[i]->event) s_features[i]->event(sdl_event);
}

static const uint32_t *present(void *ctx, int *width, int *height)
{
    (void)ctx;
    for (size_t i = 0; i < SG_FEATURE_COUNT; ++i)
        if (s_features[i]->enabled && s_features[i]->present) {
            const uint32_t *p = s_features[i]->present(width, height);
            if (p) return p;
        }
    return NULL;   /* the host shows the game's own picture */
}

/* The game's sound, just before it goes to the speakers. */
#define SG_AUDIO_RATE 48000
static void audio_mix(void *ctx, int16_t *samples, size_t count)
{
    (void)ctx;
    for (size_t i = 0; i < SG_FEATURE_COUNT; ++i)
        if (s_features[i]->enabled && s_features[i]->audio)
            s_features[i]->audio(samples, count, SG_AUDIO_RATE);
}

void sg_feature_switch(SgFeature *f, bool on)
{
    if (f->enabled == on) return;
    if (on) {
        f->enabled = true;
        sg_game_update();
        if (f->start) f->start();
    } else {
        if (f->stop) f->stop();
        f->enabled = false;
    }
    sg_log("frame %6u  %s switched %s in the menu", (unsigned)sg_game.frame, f->key, on ? "On" : "Off");
}

static CycHostExtras s_extras;

const CycHostExtras *cyc_host_extras(void)
{
    static bool ready;
    if (!ready) {
        for (size_t i = 0; i < SG_FEATURE_COUNT; ++i)
            s_features[i]->enabled = s_features[i]->default_on;
        s_extras.load_setting  = load_setting;
        s_extras.save_settings = save_settings;
        s_extras.power_on      = power_on;
        s_extras.frame_end     = frame_end;
        s_extras.input         = input;
        s_extras.event         = event;
        s_extras.present       = present;
        s_extras.audio_rate    = SG_AUDIO_RATE;   /* nesrecomp's own default too */
        s_extras.audio_mix     = audio_mix;
        sg_menu_attach(&s_extras);        /* our rows in the Esc menu (recomp-ui builds) */
        ready = true;
    }
    return &s_extras;
}
