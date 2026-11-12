# Legal and asset policy

This project builds a native Windows version of Shadowgate (NES) from a ROM
that each player supplies themselves. The game belongs to its rights holders.
Its bytes, and anything made from its bytes, are not ours to share.

## 1. Never commit

- The ROM, in any form: `.nes`, with or without its 16-byte header, zipped,
  renamed, split, compressed or encoded (for example base64).
- Build output. The `build*/` folders contain C code that nesrecomp generates
  from the ROM. Generated code is the game's code in another form.
- Graphics, text, music or maps taken from the game, including screenshots,
  pictures of tiles, text dumps and decoded message lists.
- Memory dumps, traces and save files (`.dmp`, `.bin`, `.trace`, `.sav`,
  `.state`, `.cycstate`). These can hold copies of game data.
- Disassembly listings or decompiled code of the game, in whole or in part.

## 2. What is allowed

- Our own source code, written for this project.
- Addresses, offsets, sizes and short descriptions of what they mean
  (for example "`$AB` is the cursor X position"). These record facts about
  where things are, not the game's content.
- The ROM's hashes, used to check that a player supplied the right file.
- The text table's letter mapping, as published on Data Crystal and confirmed
  in testing. It describes how letters are encoded, not the game's text.

## 3. Bring your own ROM

- Use a supported Shadowgate release (USA, Europe, Sweden, France, or Germany) from your own cartridge or dump.
- Keep it outside this folder, for example `F:\Shadowgreat\`, and point the
  build at it. Never copy it into the project, not even temporarily.
- The supported file has whole-file SHA-1
  `1af2c83b4724efbfac7fca00fab7f7a8e100caca` and body SHA-1 (header removed)
  `e7869c683bd93275cde4536cd2ddf37f230dd8d0`.

## 4. Writing code

- Describe the game's behaviour in your own words ("when a message starts,
  the game writes the text to `$0640`"). Do not paste disassembly into code,
  comments, issues or documentation.
- Write features from observed behaviour. Do not translate the game's
  routines line by line.

## 5. Enforcement

- `.gitignore` keeps the files above out of `git status`.
- `.githooks/pre-commit` refuses ROM files, archives, pictures, saves, dumps,
  build output, files over 512 KiB, anything that starts with an iNES header
  and anything whose contents match the ROM. Turn it on once per clone:

      git config core.hooksPath .githooks

- If game material is committed by mistake, stop and remove it from the
  history before pushing.

## 6. nesrecomp's licence

This project depends on nesrecomp, which uses the PolyForm Noncommercial
License 1.0.0. Anything built with it must stay noncommercial: no selling,
no paid downloads, no paywalled features.

This file states the project's rules. It is not legal advice.
