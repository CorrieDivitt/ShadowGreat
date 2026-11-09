// launcher_settings.h - what the launcher remembers (launcher.ini beside it).
#pragma once
#include <string>

struct LauncherSettings {
    std::string rom_path;          // UTF-8; the player's own ROM, never copied

    // Folders. Empty = found automatically (see paths.h).
    std::string source_folder;     // this project (CMakeLists.txt + shadowgate-map.toml)
    std::string nesrecomp_folder;
    std::string recomp_ui_folder;
    std::string tools_folder;      // llvm-mingw, Ninja, SDL2 (and CMake, later Python)
    std::string game_folder;       // where the game is built (shadowgate.exe)

    std::string built_rom_sha1;    // the ROM the current build was made from
    std::string voice_choice;      // "skip" = play without the narrator voice; "" = not asked yet
};

LauncherSettings launcher_settings_load();
void launcher_settings_save(const LauncherSettings &s);
