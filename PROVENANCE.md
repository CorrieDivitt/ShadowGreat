# Provenance

Where every piece of knowledge and every file in this project came from.
Add a line whenever a new fact or source is used.

## Reference material

| Source | What we used | Notes |
|---|---|---|
| [Data Crystal: Shadowgate](https://datacrystal.tcrf.net/wiki/Shadowgate) | Mapper 4 (MMC3), cartridge layout | Matches the ROM header |
| [Data Crystal: Shadowgate RAM map](https://datacrystal.tcrf.net/wiki/Shadowgate/RAM_map) | Cursor, selection, command, room, screen and text buffer addresses | Entries marked unverified until tested |
| Data Crystal: Shadowgate text table | Letter mapping (`7F` space, `80`-`89` digits, `8A` !, `B1`-`CA` A-Z) | |
| [nesdev wiki](https://www.nesdev.org/wiki/) | General NES hardware behaviour | |
| [mstan/nesrecomp](https://github.com/mstan/nesrecomp) | Recompiler and runtime (dependency, not copied) | PolyForm Noncommercial 1.0.0 |
| [troyedwardsjr/z2rs](https://github.com/troyedwardsjr/z2rs) | Ideas for this policy, the commit check and this file | No code copied |

## Our own observations (Mesen 2, user's own ROM)

| Date | Finding | How it was checked |
|---|---|---|
| 2026-11-05 | `$AB` cursor X, `$AC` cursor Y | Editing either byte moves the cursor |
| 2026-11-05 | Scene bounds X `$0F`-`$7F`, Y `$20`-`$90`; list positions for verbs, system and inventory | Read from RAM while moving the cursor |
| 2026-11-05 | Message text starts at `$0640` | Decoded a message on screen from the buffer |
| 2026-11-05 | `A1` is a full stop | Seen at the end of a message that ends with one |
| 2026-11-05 | `$EB`/`$EC`, `$58`/`$59` change while text prints (candidate pointers) | Watched in the memory viewer; not yet confirmed |

## Files in this repository

| Path | What it is | Origin |
|---|---|---|
| `game.toml` | nesrecomp settings for this game | Written for this project |
| `shadowgate-map.toml` | Addresses, text table and cursor positions (no game bytes) | Data Crystal plus our own measurements, per entry |
| `.gitignore`, `.gitattributes`, `.githooks/pre-commit` | Repository safety rules | Written for this project |
| `README.md`, `LEGAL.md`, `PROVENANCE.md` | Documentation | Written for this project |

## What is deliberately not here

- The ROM: each player supplies their own.
- Generated code and builds: made on the player's PC from their ROM.
- Screenshots, saves and memory dumps: kept locally only.
