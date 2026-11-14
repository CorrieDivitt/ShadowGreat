# Shadowgreat release notes

## 1.1.0

Shadowgreat now plays every European release of Shadowgate, with the narrator speaking their
languages, and builds on Linux and SteamOS too. Thanks to **Mabec-games** for contributing most of
this release (#2).

### New
- **Four more versions of the game.** Besides Shadowgate (USA), the launcher now accepts your own
  **Europe**, **Sweden**, **France** and **Germany** `.nes` files. It recognises which one you chose
  and builds the game for it, with the right 50 Hz timing for the European versions. Picking a
  different version later asks for a Rebuild.
- **The narrator speaks Swedish, German and French.** For those versions the launcher downloads a
  pronunciation list for the game's language (4 to 31 MB, checked against a fixed SHA-256 fingerprint).
  The narrators have names in the game's language, and long or run-together words are split into
  words the voice knows, so very few get skipped.
- **Mouse control for every version**, including the German one's different verb layout (#3), and a
  clickable **INSTÄLLNINGAR** line on the Swedish file and save screens.
- **Linux and SteamOS.** The launcher and the game now build and run on Linux. There's no Linux
  download yet: for now, build the launcher from source (see *Install* in the README); it then builds
  the game from your ROM as on Windows.

### Changed
- The narrator's default volume is 80 for the Swedish, German and French versions (100 for USA and
  Europe, as before).
- The mouse and the game's hand are ignored while the in-game menu is open, so clicks there no longer
  reach the game.
- `tools/package.py` checks the package against every ROM version you give it (`--rom` more than once).

### Install
- **Windows:** download `Shadowgreat-1.1.0.zip` below, unzip it to a short path (for example
  `C:\Games\Shadowgreat`), run `ShadowgreatLauncher.exe` and follow the steps. Needs Windows 10 (1803
  or later) or 11, 64-bit, about 3 GB of free disk space (about 1 GB more with the voice), and an
  internet connection for the first setup.
- **Linux / SteamOS:** build the launcher from source for now; the steps are under *Install* in the
  README.

### Updating from 1.0.0
Unzip 1.1.0 over your 1.0.0 folder, start the launcher and press **Rebuild**. Your saves, settings,
build tools and voice are kept. If you play one of the new versions with the narrator on, the
launcher downloads that language's pronunciation list once.

### Known issues
- The launcher isn't code-signed, so Windows Defender or SmartScreen may warn about it.
- The Swedish and German narrators use English voices, so they have an accent, and a few names and
  sound effects are still skipped. German has had less play-testing than the other versions; reports
  from German speakers are welcome.
- The first word of a line can occasionally sound a little odd; that's the voice model.

## 1.0.0

The first release: Shadowgate (NES) as a native Windows game, built on your own PC from your own
legally obtained **Shadowgate (USA)** `.nes` file. No part of the game is included in the download.

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
