/*
 * sg_game.c - reads the game's screen once per frame and logs changes.
 */
#include "sg_game.h"
#include "sg_features.h"
#include "cyc_mod.h"

SgGame sg_game;

static uint8_t  s_pending_id, s_pending_variant;   /* value being timed */
static unsigned s_pending_frames;                  /* how long it has held */
static bool     s_have_settled;
static uint8_t  s_settled_id;

const char *sg_screen_name(SgScreen s)
{
    switch (s) {
    case SG_SCREEN_TITLE:        return "title";
    case SG_SCREEN_FILE_SELECT:  return "file select";
    case SG_SCREEN_FILE_OPTIONS: return "file options";
    case SG_SCREEN_PLAY:         return "play";
    case SG_SCREEN_MESSAGE:      return "message";
    case SG_SCREEN_GAME_OVER:    return "game over";
    case SG_SCREEN_SAVE_CONFIRM: return "save confirm";
    default:                     return "unknown";
    }
}

static SgScreen classify(uint8_t id, uint8_t variant)
{
    switch (id) {
    case 0x10: return SG_SCREEN_TITLE;
    case 0x11:
        /* 0xB2 on USA/EUR, 0x40 on Sweden/SCN */
        if (variant == 0xB2 || variant == 0x40) return SG_SCREEN_FILE_SELECT;
        if (variant == 0xAD) return SG_SCREEN_FILE_OPTIONS;
        return SG_SCREEN_UNKNOWN;   /* never guess between the two */
    case 0x65: return SG_SCREEN_PLAY;
    case 0x6C: return SG_SCREEN_MESSAGE;
    case 0x12: return SG_SCREEN_GAME_OVER;
    case 0x13: return SG_SCREEN_SAVE_CONFIRM;
    default:   return SG_SCREEN_UNKNOWN;
    }
}

void sg_game_reset(void)
{
    sg_game.frame  = 0;
    sg_game.screen = SG_SCREEN_UNKNOWN;
    sg_game.busy   = true;
    s_pending_frames = 0;
    s_have_settled = false;
}

void sg_game_update(void)
{
    sg_game.frame++;
    sg_game.screen_id    = cyc_mod_peek(SG_RAM_SCREEN_ID);
    sg_game.menu_variant = cyc_mod_peek(SG_RAM_MENU_VARIANT);

    /* $1C only matters on the 0x11 screens; elsewhere it wanders. */
    uint8_t variant = sg_game.screen_id == 0x11 ? sg_game.menu_variant : 0;

    if (s_pending_frames > 0 &&
        sg_game.screen_id == s_pending_id && variant == s_pending_variant) {
        if (s_pending_frames < 0xFFFF) s_pending_frames++;
    } else {
        s_pending_id      = sg_game.screen_id;
        s_pending_variant = variant;
        s_pending_frames  = 1;
    }

    if (s_pending_frames == SG_SCREEN_SETTLE_FRAMES) {
        SgScreen now = classify(s_pending_id, s_pending_variant);
        bool changed = !s_have_settled || now != sg_game.screen ||
                       (now == SG_SCREEN_UNKNOWN && s_pending_id != s_settled_id);
        sg_game.screen = now;
        s_settled_id   = s_pending_id;
        s_have_settled = true;
        if (changed)
            sg_log("frame %6u  screen: %-12s ($E5=%02X  $1C=%02X)",
                   (unsigned)(sg_game.frame - (SG_SCREEN_SETTLE_FRAMES - 1)),
                   sg_screen_name(now), sg_game.screen_id, sg_game.menu_variant);
    }

    sg_game.busy = s_pending_frames < SG_SCREEN_SETTLE_FRAMES ||
                   sg_game.screen == SG_SCREEN_UNKNOWN;
}
