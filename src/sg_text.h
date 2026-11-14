/*
 * sg_text.h - turns a Shadowgate message into plain text.
 *
 * The format is described in shadowgate-map.toml ([text.packed]). Every
 * byte is read at run time from the player's own ROM through the running
 * game; nothing from the game is stored in this project.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Code locations and tables (addresses only; see shadowgate-map.toml) */
#ifndef SG_CODE_PRINT_MESSAGE
#define SG_CODE_PRINT_MESSAGE  0xC49E   /* hook site: a message starts      */
#endif
#ifndef SG_CODE_PRINT_MESSAGE_2
#define SG_CODE_PRINT_MESSAGE_2 0xC494  /* hook site: story-scene messages  */
#endif
#ifndef SG_CODE_BANK_SWAP
#define SG_CODE_BANK_SWAP      0xEAD9   /* swap in a ROM bank (A = bank)    */
#endif
#ifndef SG_TABLE_CODE_TO_TILE
#define SG_TABLE_CODE_TO_TILE  0xC7AD   /* the game's code -> tile table    */
#endif
#define SG_RAM_MESSAGE_PTR     0x0000   /* 2 bytes: where the message is    */
#define SG_WORD_BANK           4
#define SG_WORD_LIST_A         0xB000
#define SG_WORD_LIST_B         0xB800
#define SG_WORD_SLOT_BYTES     8

/* Copy the two word lists out of the running game (once per power-on).
 * Kept in memory only, never written to disk. */
bool sg_text_load_words(void);
void sg_text_forget_words(void);

/* Decode the message that starts at CPU address `addr`.
 * `spoken` gets clean text for the voice (lines joined with spaces, and
 * '\f' at each page end, where the game waits for A);
 * `logged` gets the same with " / " at line breaks and {..} for codes we
 * don't understand yet. Returns false if the message looked wrong. */
bool sg_text_decode(uint16_t addr, char *spoken, size_t spoken_size,
                    char *logged, size_t logged_size);
