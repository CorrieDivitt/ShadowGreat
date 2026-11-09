# Shadowgreat

**Shadowgate (NES) as a native Windows game, built on your own PC from your own ROM**, with mouse
control, an optional narrator voice, and options you can change while you play.

Shadowgreat is built with [nesrecomp](https://github.com/mstan/nesrecomp), which turns the NES game's
code into a native Windows program, and [recomp-ui](https://github.com/mstan/recomp-ui) for its menus.

> **No part of Shadowgate is included here or in the download.** You need your own legally obtained
> **Shadowgate (USA)** `.nes` file. The launcher checks it and builds the game from it on your PC.
> See [LEGAL.md](LEGAL.md).

---

## What's in it

### Mouse control
- **Left click** selects verbs, objects, doors, inventory items and menu choices; **right click** is the B button.
- **Hand follows the mouse:** the game's own pointing hand becomes your mouse pointer on every screen.
- A clickable **SETTINGS** line on the file select and save screens opens the Shadowgreat options, so the
  whole game can be played with just a mouse.

### Narrator voice (optional)
- Reads the **message box** aloud, using the Kokoro voice model (runs offline on your PC, no internet needed once installed).
- Stays in step with the box: it pauses at the end of each page until you press A (or click), and stops when the box closes.
- The game's **music dips** while the narrator talks, then comes back.
- Short **pauses after quotations and after "!"**, adjustable.
- Three voices: **Narrator** (deep), **Grim**, **Plain**.
- It's a separate download (about 358 MB). You can play without it and add or remove it any time.

### Options while you play
- Press **Esc** for the in-game menu. The **Shadowgreat** section has every option, and changes apply at once.
- The menu scales with the window, so it stays readable on large and 4K screens.
- **Restore Defaults** puts every Shadowgreat option back.

### The launcher
One window takes you from your ROM to playing:
1. **Your ROM:** choose your `.nes` file; it's checked to be Shadowgate (USA).
2. **Build the game:** a one-time download of the build tools (about 283 MB), then a build of a few minutes.
3. **Narrator voice (optional):** *Download voice files*, or *Play without voice*.
4. **Options:** the same options as the in-game menu, plus *Skip the game's start screen*.
5. **Play.**

Every download comes from its own project's official release on GitHub and is checked against a fixed
SHA-256 fingerprint before it's used.

### Coming next
- **Torch tracker** (in development): shows how many actions your lit torch has left, turning red when 6 or
  fewer are left. Off by default.

---

## Playing

### You need
- Windows 10 (version 1803 or later) or Windows 11, 64-bit.
- Your own **Shadowgate (USA)** `.nes` file. Other versions and hacks are refused.
- About **3 GB** of free disk space (about 1 GB more with the narrator voice).
- An internet connection for the first setup (the build tools and the voice). After that, none.

### Install
1. Download the latest `Shadowgreat-<date>.zip`.
2. Unzip it to a folder with a **short path**, for example `C:\Games\Shadowgreat`.
3. Run `ShadowgreatLauncher.exe` and follow the steps from top to bottom.

### Controls

| | Keyboard | Mouse |
|---|---|---|
| A | Z | Left click |
| B | X | Right click |
| Start | Enter | |
| Select | \ | |
| Move the cursor | Arrow keys | Move the mouse |
| Menu | Esc | SETTINGS on the file and save screens |
| Fullscreen | F11 | |
| Screenshot | F12 | |
| Save states | F7 | |
| Rewind | F8 | |
| Fast forward | Tab (hold) | |

Keys and controllers are changed on the game's start screen (see *Things to know*).

### Options

All are in the in-game menu (Esc → Shadowgreat), in the launcher's Options step, and in `config.ini` beside
`shadowgate.exe` (section `[Game]`):

| Option | `config.ini` key | Default |
|---|---|---|
| Mouse control | `Mouse` | On |
| Hand follows the mouse | `HandFollowsMouse` | On |
| Narration voice | `Narration` | On (needs the voice files) |
| Narrator | `NarratorVoice` | `Narrator` (or `Grim`, `Plain`) |
| Voice volume | `VoiceVolume` | 100 |
| Music while the narrator talks | `MusicWhileSpeaking` | 40 (100 = no dip) |
| Pause after quotations | `QuotePause` | 0.80 seconds |
| Pause after "!" | `ExclaimPause` | 0.60 seconds |
| Skip the game's start screen | `[Launcher] SkipLauncher` | 0 (off) |

---

## Things to know (from testing)

**Windows Defender or SmartScreen may warn about the launcher.**
Shadowgreat is new and isn't code-signed, and the launcher downloads and runs build tools, which is enough
for Windows' automatic guesswork to flag it. One test PC reported `Trojan:Win32/Bearfoos.A!ml`. The `!ml`
means a machine-learning guess, not a known virus. The source code is all here to check.
- SmartScreen ("Windows protected your PC"): click **More info → Run anyway**.
- Defender quarantine: Windows Security → Virus & threat protection → **Protection history** → select the
  entry → **Actions → Restore**. Only do this if you downloaded Shadowgreat from this project.

**Start the game from the launcher.**
With *Skip the game's start screen* on, double-clicking `shadowgate.exe` opens and closes straight away,
because only the start screen (or the launcher's Play button) tells it where your ROM is. If the game ever
closes at once, the launcher shows the exit code and where the log (`shadowgate.log`) is.

**Key bindings and display settings are on the game's start screen.**
That's the screen before the game, and it's shown unless you turned on *Skip the game's start screen*.

**Change the launcher's options while the game is closed.**
They're greyed out while the game is open, because the game saves its own copy of the settings when it
closes. Use the in-game menu (Esc) instead; the launcher picks up your changes when the game closes.

**Use a short install folder.**
The build tools have deep folders inside them, and very long paths can make Windows tools fail.
`C:\Games\Shadowgreat` is a good place.

**The first build takes a few minutes**, and so does a *Rebuild*. You can cancel and start again. If a
build fails, tick *Show details*; the full output is also saved in `launcher-build.log`.

**About the narrator.**
- The narration options show **Off** and are greyed out until the voice files are installed.
- The voice is made on your PC as you play, so a slow or busy PC can add a short delay before it starts
  speaking. Lines it has already spoken start at once.
- The first word of a line can occasionally sound a little odd; that's the voice model, not a bug.

**Rarely seen:** once, the window's title bar (move, resize, close) stopped responding while F11 still
worked. It hasn't happened again. If it does, press F11 twice, or close the game from the menu
(Esc → System → Quit), and please report what you were doing.

---

## Building from source (developers)

The launcher does all of this for players. To work on Shadowgreat yourself:

**Folders.** Clone these side by side, for example in `F:\NESDev`:

```
ShadowgatePort\   this repository
nesrecomp\        https://github.com/mstan/nesrecomp  (with submodules: git submodule update --init)
recomp-ui\        https://github.com/mstan/recomp-ui
Tools\            llvm-mingw, Ninja, SDL2 (MinGW), Python 3.11+ (the launcher can download these)
```

**Build the game** (portable toolchain, from PowerShell in `ShadowgatePort`):

```powershell
$env:PATH = "F:\NESDev\Tools\llvm-mingw-20261020-ucrt-x86_64\bin;F:\NESDev\Tools\ninja;" + $env:PATH
cmake -S . -B build-mingw -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ `
  -DSDL2_DIR=F:/NESDev/Tools/SDL2-2.30.0/x86_64-w64-mingw32/lib/cmake/SDL2 `
  -DNESRECOMP_ROOT=F:/NESDev/nesrecomp -DNESRECOMP_RECOMP_UI=F:/NESDev/recomp-ui `
  -DSHADOWGATE_ROM="F:/path/to/Shadowgate (USA).nes"
cmake --build build-mingw
```

Visual Studio 2022 also works: use `-G "Visual Studio 17 2022" -A x64` instead of Ninja and the compilers.

**Build the launcher:**

```powershell
cmake -S launcher -B build-launcher -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ `
  -DSDL2_DIR=F:/NESDev/Tools/SDL2-2.30.0/x86_64-w64-mingw32/lib/cmake/SDL2
cmake --build build-launcher
```

**Make the zip to share** (needs everything committed; your ROM is only read to check the zip has none of it):

```powershell
python tools\package.py --rom "F:\path\to\Shadowgate (USA).nes"
```

**Turn on the commit safety check** once after cloning. It refuses ROMs, build output, saves, dumps and
pictures of the game:

```powershell
git config core.hooksPath .githooks
```

### Project layout

| Path | What it is |
|---|---|
| `src/` | The Shadowgreat features: mouse, hand pointer, narration, menu rows, SETTINGS link, torch tracker |
| `launcher/` | The launcher (SDL2 + Dear ImGui) and `fetch.py`, which downloads the build tools and voice |
| `patches/` | Small build-time changes to recomp-ui's menu (scaling, button label) |
| `shadowgate-map.toml` | What we know about the game's memory and screens: addresses and meanings only |
| `game.toml` | nesrecomp's settings for this game |
| `voice/sg-names.txt` | Pronunciations for the narrator |
| `tools/package.py` | Makes the zip and checks it holds no game data |

---

## Licence

- Shadowgreat's own code: [MIT](LICENSE).
- **nesrecomp is PolyForm Noncommercial 1.0.0**, so Shadowgreat as a whole may be shared freely but **not sold
  or used commercially**.
- recomp-ui, Dear ImGui, recomp-net and rbengine: MIT. Lato font: OFL-1.1. SDL2: zlib. The packaged zip
  includes every licence in its `LICENSES` folder.
- Downloaded by the launcher: Python (PSF), llvm-mingw (Apache-2.0 with LLVM exception), Ninja (Apache-2.0),
  SDL2 (zlib), CMake (BSD-3-Clause), sherpa-onnx and the Kokoro model (Apache-2.0).

Shadowgate belongs to its rights holders. Shadowgreat isn't affiliated with or endorsed by them.
See [PROVENANCE.md](PROVENANCE.md) for where every fact about the game came from.
