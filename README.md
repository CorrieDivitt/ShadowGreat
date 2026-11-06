# Shadowgate Port

A native Windows version of Shadowgate (NES), built with
[nesrecomp](https://github.com/mstan/nesrecomp) from a ROM you supply.
Planned additions: mouse support and text-to-speech.

**No game data is included.** You need your own Shadowgate (USA) `.nes` file.
Read [LEGAL.md](LEGAL.md) before contributing, and see
[PROVENANCE.md](PROVENANCE.md) for where every fact came from.

## Status

Early development. The recompiled game boots, plays and saves in a window.

## Building (Windows)

Requirements: Git, Python 3.11+, CMake, Visual Studio 2022 with
"Desktop development with C++", and a clone of nesrecomp with submodules.

In Developer PowerShell for VS 2022, from the nesrecomp folder:

```powershell
cmake -S runner/cyc/project -B <this folder>\build-window `
  -G 'Visual Studio 17 2022' -A x64 `
  -DNESRECOMP_ROM='<path to your ROM>' `
  -DNESRECOMP_GAME_CONFIG='<this folder>/game.toml'
cmake --build <this folder>\build-window --config Release --parallel
```

## After cloning

Turn on the commit safety check once:

```powershell
git config core.hooksPath .githooks
```

## Licence

Noncommercial use only, because nesrecomp is licensed under PolyForm
Noncommercial 1.0.0. A licence for this project's own code is still to be chosen.
