/*
 * sg_game.h - what the game is doing right now, read once per frame.
 *
 * Shared by every feature, so each one doesn't have to read RAM itself.
 * Addresses and values come from shadowgate-map.toml (addresses only;
 * nothing from the game itself). This file only READS game memory.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* RAM addresses (see shadowgate-map.toml for what each one means) */
#define SG_RAM_SCREEN_ID     0x00E5   /* which screen is showing        */
#define SG_RAM_MENU_VARIANT  0x001C   /* tells the two 0x11 screens apart */

typedef enum SgScreen {
    SG_SCREEN_UNKNOWN = 0,   /* an $E5 value not in our map              */
    SG_SCREEN_TITLE,         /* $E5 0x10                                 */
    SG_SCREEN_FILE_SELECT,   /* $E5 0x11 and $1C 0xB2                    */
    SG_SCREEN_FILE_OPTIONS,  /* $E5 0x11 and $1C 0xAD                    */
    SG_SCREEN_PLAY,          /* $E5 0x65                                 */
    SG_SCREEN_MESSAGE,       /* $E5 0x6C (play screen with message box)  */
    SG_SCREEN_GAME_OVER,     /* $E5 0x12                                 */
    SG_SCREEN_SAVE_CONFIRM,  /* $E5 0x13                                 */
} SgScreen;

/* A screen only counts once $E5 has held the same value for this many
 * frames: between screens the game passes through other values for a
 * frame or two (see shadowgate-map.toml), and we must not act on those. */
#define SG_SCREEN_SETTLE_FRAMES 3

typedef struct SgGame {
    SgScreen screen;         /* the settled screen (what features use)    */
    bool     busy;           /* true while changing screen or on an
                                unknown one: features should do nothing  */
    uint8_t  screen_id;      /* raw $E5 this frame */
    uint8_t  menu_variant;   /* raw $1C */
    uint32_t frame;          /* frames since power on */
} SgGame;

/* Filled in at the start of each frame's feature work. */
extern SgGame sg_game;

void        sg_game_reset(void);
void        sg_game_update(void);
const char *sg_screen_name(SgScreen s);
