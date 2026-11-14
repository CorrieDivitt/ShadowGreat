# Shadowgreat release notes

## 1.0.0

The first release: Shadowgate (NES) as a native Windows game, built on your own PC from your own
legally obtained **Shadowgate** `.nes` file (USA, Europe, Sweden, France, or Germany). No part of the game is included in the download.

### Features
- **Mouse control.** Left click selects verbs, objects, doors and inventory items; right click is B.
  The game's own pointing hand follows the mouse. A clickable **SETTINGS** line on the file select and
  save screens opens the options, so the whole game can be played with just a mouse.
- **Controller pointing.** An analog stick moves the pointer (left by default, or the right one), at a
  speed you choose. A, or pressing a stick in, clicks where it points; B is on the controller's B button.
- **Narrator voice (optional).** Reads the message box aloud with the Kokoro voice model, offline on your
  PC. It keeps in step with the box, dips the music while it talks, and pauses after quotations and "!".
  Three narrators: **Dungeon Master** (default), **Old Man** and **Woman**.
- **Make the narrator your own.** Speed, Depth and Pause for each narrator, saved separately, plus a
  Narrator Options **Restore Defaults** that puts back only the voice settings and keeps your narrator.
- **Torch tracker (optional).** The actions left on each lit torch, turning orange at 15 and red at 7.
- **Options while you play.** Press Esc: the Shadowgreat section is grouped under **Control Options**,
  **Sound Options** and **Narrator Options**, which open in place. Every feature can be switched on or
  off, changes apply at once, and the menu scales with the window.
- **The launcher.** One window from ROM to playing: checks your ROM, downloads the build tools (each
  checked against a fixed SHA-256 fingerprint), builds the game, optionally downloads the narrator voice
  (about 358 MB), sets the same options as the in-game menu, and plays. Updating is unzip-over and Rebuild;
  saves, settings, tools and voice are kept.

### Install
1. Download `Shadowgreat-1.0.0.zip` below.
2. Unzip it to a short path, for example `C:\Games\Shadowgreat`.
3. Run `ShadowgreatLauncher.exe` and follow the steps.

Needs Windows 10 (1803 or later) or 11, 64-bit, about 3 GB of free disk space (about 1 GB more with the
voice), and an internet connection for the first setup.

### Known issues
- The launcher isn't code-signed, so Windows Defender or SmartScreen may warn about it (one test PC
  reported `Trojan:Win32/Bearfoos.A!ml`, a machine-learning guess). See *Things to know* in the README.
- On a slow or busy PC the narrator can start a moment late; lines it has already spoken start at once.
- The first word of a line can occasionally sound a little odd; that's the voice model.

### Licence
Shadowgreat's own code is MIT. It's built with nesrecomp (PolyForm Noncommercial 1.0.0), so Shadowgreat
may be shared freely but not sold or used commercially. Shadowgate belongs to its rights holders;
Shadowgreat isn't affiliated with or endorsed by them.
