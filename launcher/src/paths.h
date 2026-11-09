// paths.h - finding the folders and build tools the launcher works with.
#pragma once
#include "launcher_settings.h"
#include <string>

struct Folders {
    std::string source, nesrecomp, recomp_ui, tools, game;
    std::string missing;          // what couldn't be found, for the player ("" = all found)
    bool ok() const { return missing.empty(); }
};

struct Tools {
    std::string cmake, clang, clangxx, ninja, sdl2_dir, python;
    std::string missing;          // for the player, e.g. "Python, CMake"
    std::string fetch_items;      // what fetch.py should download, e.g. "cmake ninja"
    bool need_python = false;     // downloaded by the launcher itself, before fetch.py
    int download_mb = 0;          // about how much is missing
    bool ok() const { return missing.empty(); }
};

// The optional narrator voice: <game>/voice (sherpa-onnx DLLs + Kokoro model).
struct Voice {
    std::string folder;
    bool dlls = false, model = false;
    bool installed() const { return dlls && model; }
    std::string fetch_items;      // "sherpa kokoro", or what's left of it
    int download_mb = 0;
};

std::string exe_folder();                         // UTF-8, '/' separators, no trailing '/'
bool file_exists(const std::string &utf8);
Folders find_folders(const LauncherSettings &s);
Tools find_tools(const std::string &tools_folder);
Voice find_voice(const Folders &f);
std::string game_exe(const Folders &f);           // <game>/shadowgate.exe
