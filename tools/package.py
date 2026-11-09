"""package.py - make the Shadowgreat zip to share.

    python tools\\package.py --rom "F:\\Shadowgreat\\Shadowgate (USA).nes"

Puts together dist\\Shadowgreat\\ and dist\\Shadowgreat-<date>.zip:

    ShadowgreatLauncher.exe (+ SDL2.dll, libc++.dll, libunwind.dll, fetch.py, assets\\)
    source\\      this project        (the files git tracks)
    nesrecomp\\   nesrecomp           (the files git tracks, with its submodules)
    recomp-ui\\   recomp-ui           (the files git tracks)
    LICENSES\\    every licence that applies to what is in the zip
    README.txt

Only files tracked by git are taken, so build folders, saves, logs, config
files and ROMs can't get in. Then every file is checked against YOUR ROM:
the package is refused if any file contains a run of 32 bytes or more from
the ROM (also when written out as hex numbers in a text file), has a ROM
file name, or starts like a ROM. The ROM is only read for the check; nothing
from it is written anywhere.

The build tools and the narrator voice are not in the zip: the launcher
downloads them from their own projects on the player's PC.
"""
import argparse
import datetime
import os
import re
import shutil
import subprocess
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
ROM_NAMES = (".nes", ".unf", ".unif", ".fds", ".qd")
LAUNCHER_FILES = ["ShadowgreatLauncher.exe", "SDL2.dll", "libc++.dll", "libunwind.dll", "fetch.py",
                  "assets/LatoLatin-Regular.ttf"]
MATCH = 32          # bytes in a row from the ROM that count as a copy
KEY = 16            # MATCH >= 2 * KEY, so every match contains a KEY-aligned key


def fail(message):
    print("\nPACKAGE REFUSED: " + message)
    sys.exit(1)


def git(repo, *args):
    return subprocess.run(["git", "-C", repo] + list(args), check=True, capture_output=True).stdout


def tracked_files(repo):
    out = git(repo, "ls-files", "-z", "--recurse-submodules")
    return [f.decode("utf-8") for f in out.split(b"\0") if f]


def describe(repo):
    head = git(repo, "rev-parse", "HEAD").decode().strip()
    try:
        url = git(repo, "remote", "get-url", "origin").decode().strip()
    except subprocess.CalledProcessError:
        url = "(no remote)"
    changed = git(repo, "status", "--porcelain", "--untracked-files=no").decode().strip()
    return head, url, changed


def copy_repo(repo, into, skip=()):
    count = 0
    for rel in tracked_files(repo):
        if any(rel.startswith(s) for s in skip):
            continue
        src = os.path.join(repo, rel)
        if not os.path.isfile(src):             # deleted but not yet committed
            continue
        dst = os.path.join(into, rel)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copy2(src, dst)
        count += 1
    return count


def find_first(*candidates):
    for c in candidates:
        if c and os.path.isfile(c):
            return c
    return None


def find_in_tools(tools, prefix, inside):
    if not tools or not os.path.isdir(tools):
        return None
    for name in sorted(os.listdir(tools)):
        if name.startswith(prefix) and os.path.isfile(os.path.join(tools, name, inside)):
            return os.path.join(tools, name, inside)
    return None


# ---- the ROM check ---------------------------------------------------------------------
def rom_keys(rom):
    """Every 16-byte piece of the ROM (at every offset) that is varied enough
    not to turn up by chance (blank and repeating filler is skipped)."""
    keys = set()
    for i in range(0, len(rom) - KEY + 1):
        k = rom[i:i + KEY]
        if len(set(k)) >= 6:
            keys.add(k)
    return keys


HEX_RUN = re.compile(rb"(?:(?:0x|\$)?[0-9A-Fa-f]{2}(?![0-9A-Fa-f])[\s,;]*){%d,}" % MATCH)
HEX_BYTE = re.compile(rb"(?:0x|\$)?([0-9A-Fa-f]{2})(?![0-9A-Fa-f])")


def copies_in(data, rom, keys):
    """Offsets in data where 32+ bytes of the ROM appear."""
    found = []
    at = 0
    while at + KEY <= len(data):
        k = data[at:at + KEY]
        at += KEY
        if k not in keys:
            continue
        at -= KEY
        start = rom.find(k)
        while start != -1:
            # grow the match both ways and see if it reaches MATCH bytes
            a, b = at, start
            while a > 0 and b > 0 and data[a - 1] == rom[b - 1]:
                a -= 1; b -= 1
            e, f = at + KEY, start + KEY
            while e < len(data) and f < len(rom) and data[e] == rom[f]:
                e += 1; f += 1
            if e - a >= MATCH:
                found.append((a, e - a, b))
                break
            start = rom.find(k, start + 1)
        at = max(at + KEY, (found[-1][0] + found[-1][1]) if found else 0)   # past this match
    return found


def check_package(folder, rom_path):
    with open(rom_path, "rb") as f:
        whole = f.read()
    if whole[:4] != b"NES\x1a":
        fail("%s doesn't look like a .nes file." % rom_path)
    rom = whole[16:]
    print("Checking every file against your ROM (%d bytes)..." % len(rom))
    keys = rom_keys(rom)
    problems, checked = [], 0
    for base, _, names in os.walk(folder):
        for name in names:
            path = os.path.join(base, name)
            rel = os.path.relpath(path, folder)
            with open(path, "rb") as f:
                data = f.read()
            checked += 1
            if name.lower().endswith(ROM_NAMES):
                problems.append("%s: a ROM file name" % rel)
            if data[:4] == b"NES\x1a":
                problems.append("%s: starts like a ROM" % rel)
            for a, n, b in copies_in(data, rom, keys)[:3]:
                problems.append("%s: %d bytes at offset %d match the ROM at $%X" % (rel, n, a, b))
            for run in HEX_RUN.finditer(data):           # bytes written as hex in text
                as_bytes = bytes(int(h, 16) for h in HEX_BYTE.findall(run.group(0)))
                for a, n, b in copies_in(as_bytes, rom, keys)[:1]:
                    problems.append("%s: hex numbers near offset %d match %d bytes of the ROM"
                                    % (rel, run.start(), n))
    if problems:
        fail("game data found:\n  " + "\n  ".join(problems))
    print("  %d files checked: no game data." % checked)


# ---- putting it together ------------------------------------------------------------------
README = """Shadowgreat - Shadowgate (NES) for Windows, built on your PC from your own ROM
==========================================================================

This contains NO part of Shadowgate. You need your own legally obtained
Shadowgate (USA) .nes file; the launcher checks it and builds the game from
it on your PC.

How to play
  1. Unzip this folder somewhere with a short path, e.g. C:\\Games\\Shadowgreat
  2. Run ShadowgreatLauncher.exe
  3. Choose your Shadowgate (USA) .nes file
  4. Download the build tools (one time, about 283 MB) and build the game
     (a few minutes, one time)
  5. Optional: download the narrator voice (about 358 MB), or play without it
  6. Choose your options and press Play

Needs Windows 10 (version 1803 or later) or Windows 11, 64-bit, and about
3 GB of free disk space (about 1 GB more with the voice).

What gets downloaded, and from where (each is checked against a fixed
SHA-256 fingerprint before it is used):
  Python 3.13      github.com/astral-sh/python-build-standalone   (PSF licence)
  llvm-mingw       github.com/mstorsjo/llvm-mingw   (Apache-2.0 with LLVM exception)
  Ninja            github.com/ninja-build/ninja     (Apache-2.0)
  SDL2             github.com/libsdl-org/SDL        (zlib)
  CMake            github.com/Kitware/CMake         (BSD-3-Clause)
  sherpa-onnx      github.com/k2-fsa/sherpa-onnx    (Apache-2.0)   voice only
  Kokoro model     github.com/k2-fsa/sherpa-onnx    (Apache-2.0)   voice only

Licences: see the LICENSES folder. nesrecomp is under the PolyForm
Noncommercial licence 1.0.0, so this package may be shared, but not sold or
used commercially.

Versions in this package:
%s
"""


def main():
    ap = argparse.ArgumentParser(description="Make the Shadowgreat zip to share.")
    ap.add_argument("--rom", required=True, help="your Shadowgate (USA) .nes, used only for the check")
    ap.add_argument("--nesrecomp", default=os.path.join(os.path.dirname(ROOT), "nesrecomp"))
    ap.add_argument("--recomp-ui", default=os.path.join(os.path.dirname(ROOT), "recomp-ui"))
    ap.add_argument("--launcher", default=os.path.join(ROOT, "build-launcher"),
                    help="the folder ShadowgreatLauncher.exe was built in")
    ap.add_argument("--tools", default=os.path.join(os.path.dirname(ROOT), "Tools"),
                    help="the build tools folder (for the SDL2 and llvm-mingw licence texts)")
    ap.add_argument("--allow-uncommitted", action="store_true",
                    help="package even if a project has changes that aren't committed")
    args = ap.parse_args()

    repos = [("source", ROOT, ()), ("nesrecomp", args.nesrecomp, ()), ("recomp-ui", args.recomp_ui, ())]
    versions = []
    for name, repo, _ in repos:
        if not os.path.isdir(os.path.join(repo, ".git")) and not os.path.isfile(os.path.join(repo, ".git")):
            fail("%s isn't a git checkout: %s" % (name, repo))
        head, url, changed = describe(repo)
        versions.append("  %-10s %s  %s" % (name, head[:12], url))
        if changed and not args.allow_uncommitted:
            fail("%s has changes that aren't committed:\n%s\nCommit them first (or use --allow-uncommitted)."
                 % (name, changed))
    for f in LAUNCHER_FILES:
        if not os.path.isfile(os.path.join(args.launcher, f)):
            fail("%s is missing from %s. Build the launcher first." % (f, args.launcher))

    dist = os.path.join(ROOT, "dist")
    out = os.path.join(dist, "Shadowgreat")
    if os.path.isdir(out):
        shutil.rmtree(out)
    os.makedirs(out)

    for f in LAUNCHER_FILES:
        dst = os.path.join(out, f)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copy2(os.path.join(args.launcher, f), dst)
    for name, repo, skip in repos:
        n = copy_repo(repo, os.path.join(out, name), skip)
        print("  %-10s %d files" % (name, n))

    # Licences
    lic = os.path.join(out, "LICENSES")
    os.makedirs(lic)
    copies = [
        ("Shadowgreat.txt", find_first(os.path.join(ROOT, "LICENSE"), os.path.join(ROOT, "LICENSE.md"))),
        ("nesrecomp (PolyForm Noncommercial 1.0.0).txt", os.path.join(args.nesrecomp, "LICENSE")),
        ("recomp-ui (MIT).txt", os.path.join(args.recomp_ui, "LICENSE")),
        ("recomp-net (MIT).txt", os.path.join(args.nesrecomp, "lib", "recomp-net", "LICENSE")),
        ("retcomm-rbengine (MIT).txt", os.path.join(args.nesrecomp, "lib", "retcomm-rbengine", "LICENSE")),
        ("Dear ImGui (MIT).txt", os.path.join(args.recomp_ui, "src", "third_party", "imgui", "LICENSE.txt")),
        ("sherpa-onnx C API header (Apache-2.0).txt", os.path.join(ROOT, "third_party", "sherpa-onnx", "LICENSE")),
        ("Lato font (OFL-1.1).txt", os.path.join(ROOT, "launcher", "licenses", "OFL-Lato.txt")),
        ("SDL2.dll (zlib).txt", find_in_tools(args.tools, "SDL2-", "COPYING.txt")),
        ("libc++ and libunwind DLLs (Apache-2.0 with LLVM exception).txt",
         find_in_tools(args.tools, "llvm-mingw", "LICENSE.TXT")),
    ]
    missing = [name for name, src in copies if not src or not os.path.isfile(src)]
    if missing:
        fail("licence texts not found: " + ", ".join(missing))
    for name, src in copies:
        shutil.copy2(src, os.path.join(lic, name))

    with open(os.path.join(out, "README.txt"), "w", encoding="utf-8", newline="\r\n") as f:
        f.write(README % "\n".join(versions))

    check_package(out, args.rom)

    stamp = datetime.date.today().isoformat()
    zip_path = os.path.join(dist, "Shadowgreat-%s.zip" % stamp)
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for base, _, names in os.walk(out):
            for name in sorted(names):
                path = os.path.join(base, name)
                z.write(path, os.path.join("Shadowgreat", os.path.relpath(path, out)))
    print("\nMade %s (%.1f MB)" % (zip_path, os.path.getsize(zip_path) / 1048576))
    print("The folder it was made from is %s" % out)


if __name__ == "__main__":
    main()
