/*
 * sg_menu.c - Shadowgate's rows in the in-game menu (Esc), p8-livemenu.
 *
 * The menu itself is nesrecomp's, drawn by recomp-ui (an optional add-on:
 * configure with -DNESRECOMP_RECOMP_UI=<folder>). We only list our rows and
 * answer "what is this set to?" / "set it to this". Every change applies at
 * once, while the game runs, and is saved to config.ini [Game] with the
 * rest of the settings.
 *
 * Without recomp-ui this file does nothing (the settings stay in config.ini).
 */
#include "cyc_host_extras.h"
#include "sg_features.h"
#include "sg_voice.h"

#include <string.h>

#if defined(CYC_WITH_RECOMP_UI)
#include "recomp_runtime_ui.h"

#define SECTION "Shadowgreat"

static const char *s_voice_names[8];
static const char *s_stick_names[] = { "Left stick", "Right stick" };

/* Descriptions are one line in the menu and are cut off at the edge, so
 * they are kept to about 40 characters. Pauses are whole numbers of
 * milliseconds so the rows get the menu's - / + buttons. */
static RecompRuntimeUiItem s_items[] = {
    { "sg.mouse", SECTION, "Mouse control",
      "Left click selects, right click is B.",
      RECOMP_RUNTIME_UI_BOOL, 0, 1, 1, NULL, 0, NULL },
    { "sg.hand", SECTION, "Hand follows the mouse",
      "The game's hand is the pointer.",
      RECOMP_RUNTIME_UI_BOOL, 0, 1, 1, NULL, 0, NULL },
    { "sg.narration", SECTION, "Narration voice",
      "Reads the message box aloud.",
      RECOMP_RUNTIME_UI_BOOL, 0, 1, 1, NULL, 0, NULL },
    { "sg.voice", SECTION, "Narrator",
      "A new voice starts with the next message.",
      RECOMP_RUNTIME_UI_CHOICE, 0, 0, 1, s_voice_names, 0, NULL },
    { "sg.voice.volume", SECTION, "Voice volume",
      "How loud the narrator is.",
      RECOMP_RUNTIME_UI_INT, 0, 100, 5, NULL, 0, NULL },
    { "sg.voice.music", SECTION, "Music while the narrator talks",
      "Game sound during speech (100 = no dip).",
      RECOMP_RUNTIME_UI_INT, 0, 100, 5, NULL, 0, NULL },
    { "sg.voice.quote", SECTION, "Pause after quotations (ms)",
      "Quiet after a closing quote.",
      RECOMP_RUNTIME_UI_INT, 0, 2000, 100, NULL, 0, NULL },
    { "sg.voice.exclaim", SECTION, "Pause after \"!\" (ms)",
      "Quiet after an exclamation mark.",
      RECOMP_RUNTIME_UI_INT, 0, 2000, 100, NULL, 0, NULL },
    { "sg.torch", SECTION, "Torch tracker",
      "Actions left on each lit torch.",
      RECOMP_RUNTIME_UI_BOOL, 0, 1, 1, NULL, 0, NULL },
    { "sg.pad", SECTION, "Stick moves the pointer",
      "Point with a stick; A or a stick press clicks.",
      RECOMP_RUNTIME_UI_BOOL, 0, 1, 1, NULL, 0, NULL },
    { "sg.pad.stick", SECTION, "Pointer stick",
      "Which stick moves the pointer.",
      RECOMP_RUNTIME_UI_CHOICE, 0, 1, 1, s_stick_names, 2, NULL },
    { "sg.pad.speed", SECTION, "Pointer speed",
      "How fast the stick moves it.",
      RECOMP_RUNTIME_UI_INT, 1, 10, 1, NULL, 0, NULL },
    { "sg.defaults", SECTION, "Defaults",
      "Restore Shadowgreat default options",
      RECOMP_RUNTIME_UI_ACTION, 0, 0, 0, NULL, 0, NULL },
};
#define ITEM_COUNT (sizeof s_items / sizeof s_items[0])

static bool is(const RecompRuntimeUiItem *it, const char *key) { return it->key && !strcmp(it->key, key); }

static int get_value(void *ctx, const RecompRuntimeUiItem *it, int *out)
{
    (void)ctx;
    if (is(it, "sg.mouse"))              *out = sg_feature_mouse.enabled;
    else if (is(it, "sg.hand"))          *out = sg_feature_hand.enabled;
    /* Shown Off while the voice package is missing (the row is greyed then);
     * the saved setting itself stays as the player chose it. */
    else if (is(it, "sg.narration"))     *out = sg_feature_narration.enabled && sg_voice_built_in();
    else if (is(it, "sg.torch"))         *out = sg_feature_torch.enabled;
    else if (is(it, "sg.pad"))           *out = sg_feature_pad.enabled;
    else if (is(it, "sg.pad.stick"))     *out = sg_pad_get_stick();
    else if (is(it, "sg.pad.speed"))     *out = sg_pad_get_speed();
    else if (is(it, "sg.voice"))         *out = sg_narration_get(SG_NARR_PRESET);
    else if (is(it, "sg.voice.volume"))  *out = sg_narration_get(SG_NARR_VOLUME);
    else if (is(it, "sg.voice.music"))   *out = sg_narration_get(SG_NARR_MUSIC);
    else if (is(it, "sg.voice.quote"))   *out = 100 * sg_narration_get(SG_NARR_QUOTE_TENTHS);
    else if (is(it, "sg.voice.exclaim")) *out = 100 * sg_narration_get(SG_NARR_EXCLAIM_TENTHS);
    else return 0;
    return 1;
}

static int set_value(void *ctx, const RecompRuntimeUiItem *it, int v)
{
    (void)ctx;
    if (is(it, "sg.mouse"))              sg_feature_switch(&sg_feature_mouse, v != 0);
    else if (is(it, "sg.hand"))          sg_feature_switch(&sg_feature_hand, v != 0);
    else if (is(it, "sg.narration"))     sg_feature_switch(&sg_feature_narration, v != 0);
    else if (is(it, "sg.torch"))         sg_feature_switch(&sg_feature_torch, v != 0);
    else if (is(it, "sg.pad"))           sg_feature_switch(&sg_feature_pad, v != 0);
    else if (is(it, "sg.pad.stick"))     sg_pad_set_stick(v);
    else if (is(it, "sg.pad.speed"))     sg_pad_set_speed(v);
    else if (is(it, "sg.voice"))         sg_narration_set(SG_NARR_PRESET, v);
    else if (is(it, "sg.voice.volume"))  sg_narration_set(SG_NARR_VOLUME, v);
    else if (is(it, "sg.voice.music"))   sg_narration_set(SG_NARR_MUSIC, v);
    else if (is(it, "sg.voice.quote"))   sg_narration_set(SG_NARR_QUOTE_TENTHS, (v + 50) / 100);
    else if (is(it, "sg.voice.exclaim")) sg_narration_set(SG_NARR_EXCLAIM_TENTHS, (v + 50) / 100);
    else return 0;
    return 1;
}

static int run_action(void *ctx, const RecompRuntimeUiItem *it)
{
    (void)ctx;
    if (!is(it, "sg.defaults")) return 0;
    sg_log("menu: restoring the Shadowgreat defaults");
    sg_feature_switch(&sg_feature_mouse, sg_feature_mouse.default_on);
    sg_feature_switch(&sg_feature_hand, sg_feature_hand.default_on);
    sg_feature_switch(&sg_feature_narration, sg_feature_narration.default_on);
    sg_feature_switch(&sg_feature_torch, sg_feature_torch.default_on);
    sg_feature_switch(&sg_feature_pad, sg_feature_pad.default_on);
    sg_pad_defaults();
    sg_narration_defaults();
    return 1;
}

/* Rows that depend on another switch are greyed out while it is Off. */
static int is_enabled(void *ctx, const RecompRuntimeUiItem *it)
{
    (void)ctx;
    if (is(it, "sg.hand")) return sg_feature_mouse.enabled;
    if (is(it, "sg.pad")) return sg_feature_mouse.enabled;
    if (is(it, "sg.pad.stick") || is(it, "sg.pad.speed"))
        return sg_feature_mouse.enabled && sg_feature_pad.enabled;
    if (is(it, "sg.narration")) return sg_voice_built_in();
    if (it->key && !strncmp(it->key, "sg.voice", 8))
        return sg_voice_built_in() && sg_feature_narration.enabled;
    return 1;
}

static RecompRuntimeUiCallbacks s_callbacks;

void sg_menu_attach(struct CycHostExtras *x)
{
    int n = sg_voice_preset_count < 8 ? sg_voice_preset_count : 8;
    for (int i = 0; i < n; ++i) s_voice_names[i] = sg_voice_presets[i].name;
    for (size_t i = 0; i < ITEM_COUNT; ++i)
        if (is(&s_items[i], "sg.voice")) {
            s_items[i].maximum = n - 1;
            s_items[i].choice_count = (size_t)n;
        }
    memset(&s_callbacks, 0, sizeof s_callbacks);
    s_callbacks.get_value = get_value;
    s_callbacks.set_value = set_value;
    s_callbacks.is_enabled = is_enabled;
    s_callbacks.run_action = run_action;
    x->menu_items = s_items;
    x->menu_item_count = ITEM_COUNT;
    x->menu_callbacks = &s_callbacks;
}

#else   /* built without recomp-ui: no menu */

void sg_menu_attach(struct CycHostExtras *x) { (void)x; }

#endif
