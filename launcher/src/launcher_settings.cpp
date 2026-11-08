// launcher_settings.cpp - launcher.ini: plain "key = value" lines.
#include "launcher_settings.h"
#include "utf8_file.h"

#include <SDL.h>
#include <cstdio>
#include <cstring>

static std::string ini_path()
{
    std::string path = "launcher.ini";
    if (char *base = SDL_GetBasePath()) {
        path = std::string(base) + "launcher.ini";
        SDL_free(base);
    }
    return path;
}

static std::string trim(const std::string &s)
{
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

LauncherSettings launcher_settings_load()
{
    LauncherSettings s;
    FILE *f = utf8_fopen(ini_path().c_str(), "rb");
    if (!f) return s;
    char line[2048];
    while (std::fgets(line, sizeof line, f)) {
        std::string l(line);
        size_t eq = l.find('=');
        if (eq == std::string::npos || l[0] == '#' || l[0] == ';') continue;
        std::string key = trim(l.substr(0, eq)), value = trim(l.substr(eq + 1));
        if (key == "RomPath") s.rom_path = value;
    }
    std::fclose(f);
    return s;
}

void launcher_settings_save(const LauncherSettings &s)
{
    FILE *f = utf8_fopen(ini_path().c_str(), "wb");
    if (!f) return;
    std::fprintf(f, "# Shadowgreat launcher settings\n");
    std::fprintf(f, "RomPath = %s\n", s.rom_path.c_str());
    std::fclose(f);
}
