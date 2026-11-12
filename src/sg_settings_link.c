/*
 * sg_settings_link.c - a clickable SETTINGS line on the file select and save
 * screens, so a mouse-only player can reach the Shadowgreat options.
 *
 * Part of Mouse control (feat_mouse.c draws and clicks it). It is not part of
 * the game: the game's own up/down choice never lands on it, and nothing is
 * written to the game's memory. Clicking it opens the in-game menu (recomp-ui)
 * straight at the Shadowgreat section; in a build without recomp-ui it is
 * not shown.
 *
 * The letters are the game's own, read from the pattern table the screen is
 * using right now, so they match the game's lettering and nothing from the
 * game is stored here. These screens use their own letter set (not the
 * message box's, and not in A-Z order); the tile numbers of the letters we
 * need are listed in shadowgate-map.toml [settings_link]. Before drawing we
 * check that the screen's own word (FILE / SAVE) uses the tiles we expect,
 * so a different letter set is never drawn as nonsense.
 */
#include "sg_settings_link.h"
#include "sg_features.h"
#include "sg_game.h"
#include "cyc_render.h"

#include <string.h>

#if defined(CYC_WITH_RECOMP_UI)
#include "recomp_runtime_ui.h"
RecompRuntimeUi *cyc_ui_runtime(void);   /* nesrecomp runner/cyc/cyc_ui_menu.c */
#define SG_LINK_AVAILABLE 1
#else
#define SG_LINK_AVAILABLE 0
#endif

/* nesrecomp's NES colour table (also used by feat_hand.c). */
extern uint32_t hw_palette_argb[512];

#define WORD          "SETTINGS"
#define WORD_LEN      8
#define HOVER_COLOR   0x30          /* NES white while the pointer is on it */

/* Where it goes (top-left pixel of the first letter), measured from
 * screenshots 2026-11-08. The dark doorway runs from x 80 to 175, so the
 * 64-pixel word lines up with FILE / SAVE? at x 112 and just fits. */
/* Tile numbers in these screens' letter set (found in the ROM's CHR by
 * matching the letters seen on screen, 2026-11-08; shadowgate-map.toml). */
static const uint8_t SETTINGS_TILES[WORD_LEN] = {
    0xB6, 0x9F, 0x58, 0x58, 0x59, 0x4B, 0xC1, 0xB6,      /* S E T T I N G S */
};

typedef struct {
    SgScreen screen;
    int x, y;                 /* where SETTINGS goes                          */
    int known_col, known_row; /* a word the screen shows, at this tile column/row */
    uint8_t known[4];         /* ... and the tiles it should be made of        */
} Place;
static const Place PLACES[] = {
    /* below FILE 3 / NEW; FILE 1 is at (112,120) */
    { SG_SCREEN_FILE_SELECT,  112, 176, 14, 15, { 0x5A, 0x59, 0xB4, 0x9F } },  /* F I L E */
    /* below NO; SAVE? is at (112,112) */
    { SG_SCREEN_SAVE_CONFIRM, 112, 168, 14, 14, { 0xB6, 0xF6, 0x5D, 0x9F } },  /* S A V E (USA/Europe) */
    { SG_SCREEN_SAVE_CONFIRM, 112, 168, 14, 14, { 0xB6, 0xFD, 0xF6, 0x9D } },  /* S P A R (Sweden) */
};

/* Does the screen show its word with the tiles we expect? */
static bool letters_match(const Place *p)
{
    static const uint16_t tables[] = { 0x2000, 0x2400, 0x2800, 0x2C00 };
    for (size_t t = 0; t < sizeof tables / sizeof tables[0]; ++t) {
        uint16_t at = (uint16_t)(tables[t] + p->known_row * 32 + p->known_col);
        bool ok = true;
        for (int i = 0; ok && i < 4; ++i) ok = cyc_render_nametable((uint16_t)(at + i)) == p->known[i];
        if (ok) return true;
    }
    return false;
}

static const Place *place_now(void)
{
    if (!SG_LINK_AVAILABLE || !sg_feature_mouse.enabled || sg_game.busy) return NULL;
    for (size_t i = 0; i < sizeof PLACES / sizeof PLACES[0]; ++i)
        if (PLACES[i].screen == sg_game.screen && letters_match(&PLACES[i])) return &PLACES[i];
    return NULL;
}

static bool s_drawn;   /* only clickable while it is actually shown */

bool sg_settings_link_hit(int x, int y)
{
    const Place *p = place_now();
    return p && s_drawn && x >= p->x - 2 && x < p->x + WORD_LEN * 8 + 2 && y >= p->y - 3 && y < p->y + 8 + 3;
}

bool sg_settings_link_open(void)
{
#if SG_LINK_AVAILABLE
    RecompRuntimeUi *ui = cyc_ui_runtime();
    return ui && recomp_runtime_ui_open_section(ui, "Shadowgreat");
#else
    return false;
#endif
}

/* The colour the game's own text has on this screen: the commonest
 * non-black colour in the text area above our line. */
static uint32_t text_color(const uint32_t *buf, int width, int x0, const Place *p)
{
    uint32_t seen[8] = { 0 };
    int count[8] = { 0 }, kinds = 0;
    uint32_t black = buf[(p->y + 4) * width + x0 + 90];   /* the doorway's dark */
    for (int y = p->y - 32; y < p->y - 8; ++y)
        for (int x = p->x; x < p->x + 48; ++x) {
            uint32_t c = buf[y * width + x0 + x];
            if (c == black) continue;
            int k = 0;
            while (k < kinds && seen[k] != c) ++k;
            if (k == kinds) { if (kinds == 8) continue; seen[kinds++] = c; }
            count[k]++;
        }
    int best = -1;
    for (int k = 0; k < kinds; ++k) if (best < 0 || count[k] > count[best]) best = k;
    return best >= 0 ? seen[best] : hw_palette_argb[0x10];
}

void sg_settings_link_draw(uint32_t *buf, int width, int height, int x0)
{
    const Place *p = place_now();
    if (!p || height < 240) return;
    static SgScreen s_logged = SG_SCREEN_UNKNOWN;
    if (!letters_match(p)) {
        if (s_logged != p->screen) {
            s_logged = p->screen;
            sg_log("settings link: the %s screen's letters are not the expected ones; not drawn",
                   sg_screen_name(p->screen));
        }
        s_drawn = false;
        return;
    }
    s_drawn = true;
    bool hover = sg_settings_link_hit(sg_pointer_x, sg_pointer_y);
    uint32_t color = hover ? hw_palette_argb[HOVER_COLOR] : text_color(buf, width, x0, p);
    uint16_t table = cyc_render_line_bg_table(p->y - 24);   /* where the game's text is */
    for (int i = 0; i < WORD_LEN; ++i) {
        uint16_t pattern = (uint16_t)(table + SETTINGS_TILES[i] * 16);
        for (int r = 0; r < 8; ++r) {
            uint8_t lo = cyc_render_chr((uint16_t)(pattern + r));
            uint8_t hi = cyc_render_chr((uint16_t)(pattern + r + 8));
            for (int c = 0; c < 8; ++c) {
                if (!(((lo | hi) >> (7 - c)) & 1)) continue;
                int x = p->x + i * 8 + c, y = p->y + r;
                buf[y * width + x0 + x] = color;
            }
        }
    }
}
