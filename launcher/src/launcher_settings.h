// launcher_settings.h - what the launcher remembers (launcher.ini beside it).
#pragma once
#include <string>

struct LauncherSettings {
    std::string rom_path;   // UTF-8; the player's own ROM, never copied
};

LauncherSettings launcher_settings_load();
void launcher_settings_save(const LauncherSettings &s);
