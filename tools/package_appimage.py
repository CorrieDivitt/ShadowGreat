#!/usr/bin/env python3
"""package_appimage.py - Build and package Shadowgreat as a standalone Linux/SteamOS AppImage.

Usage:
    python tools/package_appimage.py [--output-dir dist]
"""
import argparse
import os
import shutil
import stat
import subprocess
import sys
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BASE = ROOT.parent
NESRECOMP = BASE / "nesrecomp"
RECOMP_UI = BASE / "recomp-ui"
APPIMAGETOOL_URL = "https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage"


def get_version() -> str:
    version_file = ROOT / "VERSION"
    if version_file.exists():
        return version_file.read_text(encoding="utf-8").strip()
    return "1.0.0"


def git_tracked_files(repo_path: Path) -> list[str]:
    cmd = ["git", "-C", str(repo_path), "ls-files", "-z", "--recurse-submodules"]
    out = subprocess.check_output(cmd)
    return [f.decode("utf-8") for f in out.split(b"\0") if f]


def copy_git_repo(src_repo: Path, dst_dir: Path, skip_prefixes: tuple = ()):
    files = git_tracked_files(src_repo)
    for rel in files:
        if any(rel.startswith(p) for p in skip_prefixes):
            continue
        src = src_repo / rel
        if not src.is_file():
            continue
        dst = dst_dir / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)


def ensure_appimagetool() -> Path:
    which = shutil.which("appimagetool")
    if which:
        return Path(which)
    tmp_tool = Path("/tmp/appimagetool")
    if tmp_tool.exists() and os.access(tmp_tool, os.X_OK):
        return tmp_tool
    print(f"Downloading appimagetool from {APPIMAGETOOL_URL}...")
    urllib.request.urlretrieve(APPIMAGETOOL_URL, tmp_tool)
    tmp_tool.chmod(tmp_tool.stat().st_mode | stat.S_IEXEC)
    return tmp_tool


def build_launcher(build_dir: Path, recomp_ui_dir: Path) -> Path:
    build_dir.mkdir(parents=True, exist_ok=True)
    cmake_cmd = [
        "cmake", "-S", str(ROOT / "launcher"), "-B", str(build_dir),
        "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
        f"-DRECOMP_UI_DIR={recomp_ui_dir}"
    ]
    subprocess.check_call(cmake_cmd)
    subprocess.check_call(["ninja", "-C", str(build_dir)])
    exe = build_dir / "ShadowgreatLauncher"
    if not exe.exists():
        raise FileNotFoundError(f"Launcher executable not found at {exe}")
    return exe


def main():
    parser = argparse.ArgumentParser(description="Package Shadowgreat as an AppImage.")
    parser.add_argument("--output-dir", default=str(ROOT / "dist"), help="Output directory")
    parser.add_argument("--nesrecomp-dir", default=None, help="Path to nesrecomp repository")
    parser.add_argument("--recomp-ui-dir", default=None, help="Path to recomp-ui repository")
    args = parser.parse_args()

    nesrecomp_dir = (
        Path(args.nesrecomp_dir).resolve()
        if args.nesrecomp_dir
        else (ROOT / "nesrecomp" if (ROOT / "nesrecomp").exists() else BASE / "nesrecomp")
    )
    recomp_ui_dir = (
        Path(args.recomp_ui_dir).resolve()
        if args.recomp_ui_dir
        else (ROOT / "recomp-ui" if (ROOT / "recomp-ui").exists() else BASE / "recomp-ui")
    )

    if not nesrecomp_dir.exists():
        raise FileNotFoundError(f"nesrecomp directory not found at {nesrecomp_dir}")
    if not recomp_ui_dir.exists():
        raise FileNotFoundError(f"recomp-ui directory not found at {recomp_ui_dir}")

    version = get_version()
    print(f"=== Packaging Shadowgreat v{version} as Linux/SteamOS AppImage ===")

    output_dir = Path(args.output_dir).resolve()
    output_dir.mkdir(parents=True, exist_ok=True)

    build_dir = ROOT / "build-launcher-release"
    launcher_exe = build_launcher(build_dir, recomp_ui_dir)

    app_dir = output_dir / "AppDir"
    if app_dir.exists():
        shutil.rmtree(app_dir)
    app_dir.mkdir(parents=True, exist_ok=True)

    usr_bin = app_dir / "usr" / "bin"
    usr_bin.mkdir(parents=True, exist_ok=True)

    # Copy launcher executable
    shutil.copy2(launcher_exe, usr_bin / "ShadowgreatLauncher")

    # Copy assets
    lato_src = recomp_ui_dir / "assets" / "LatoLatin-Regular.ttf"
    for dest in (app_dir / "assets", usr_bin / "assets"):
        dest.mkdir(parents=True, exist_ok=True)
        if lato_src.exists():
            shutil.copy2(lato_src, dest / "LatoLatin-Regular.ttf")

    # Copy fetch.py
    fetch_src = ROOT / "launcher" / "fetch.py"
    shutil.copy2(fetch_src, app_dir / "fetch.py")
    shutil.copy2(fetch_src, usr_bin / "fetch.py")

    # Copy repositories
    print("Bundling Shadowgreat source...")
    copy_git_repo(ROOT, app_dir / "source", skip_prefixes=("dist", "build"))
    print("Bundling nesrecomp source...")
    copy_git_repo(nesrecomp_dir, app_dir / "nesrecomp")
    print("Bundling recomp-ui source...")
    copy_git_repo(recomp_ui_dir, app_dir / "recomp-ui")

    # Desktop entry & Icon
    shutil.copy2(ROOT / "shadowgreat.desktop", app_dir / "shadowgreat.desktop")
    shutil.copy2(ROOT / "shadowgreat.png", app_dir / "shadowgreat.png")

    # AppRun entrypoint
    apprun = app_dir / "AppRun"
    apprun.write_text("""#!/bin/sh
set -e

HERE="$(dirname "$(readlink -f "$0")")"
export APPDIR="$HERE"
export PATH="$HERE/usr/bin:$PATH"

export XDG_DATA_HOME="${XDG_DATA_HOME:-$HOME/.local/share}"
export XDG_CONFIG_HOME="${XDG_CONFIG_HOME:-$HOME/.config}"
mkdir -p "$XDG_DATA_HOME/shadowgreat" "$XDG_CONFIG_HOME/shadowgreat"

cd "$HERE"
exec "$HERE/usr/bin/ShadowgreatLauncher" "$@"
""", encoding="utf-8")
    apprun.chmod(apprun.stat().st_mode | stat.S_IEXEC | stat.S_IXGRP | stat.S_IXOTH)

    # Build AppImage with appimagetool
    appimagetool = ensure_appimagetool()
    appimage_out = output_dir / f"Shadowgreat-{version}-x86_64.AppImage"
    if appimage_out.exists():
        appimage_out.unlink()
    print(f"Creating {appimage_out}...")

    env = os.environ.copy()
    env["ARCH"] = "x86_64"
    cmd = [str(appimagetool), "--appimage-extract-and-run", str(app_dir), str(appimage_out)]
    subprocess.check_call(cmd, env=env)

    print(f"\nSUCCESS: Created {appimage_out} ({appimage_out.stat().st_size // (1024 * 1024)} MB)")


if __name__ == "__main__":
    main()
