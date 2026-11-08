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
    std::string cmake, clang, clangxx, ninja, sdl2_dir;
    std::string missing;
    bool ok() const { return missing.empty(); }
};

std::string exe_folder();                         // UTF-8, '/' separators, no trailing '/'
bool file_exists(const std::string &utf8);
Folders find_folders(const LauncherSettings &s);
Tools find_tools(const std::string &tools_folder);
std::string game_exe(const Folders &f);           // <game>/shadowgate.exe
