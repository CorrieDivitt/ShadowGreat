// launcher_settings.cpp - launcher.ini: plain "key = value" lines.
#include "launcher_settings.h"
#include "utf8_file.h"

#include <SDL.h>
#include <cstdio>

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

namespace {
struct Key { const char *name; std::string LauncherSettings::*field; };
const Key kKeys[] = {
    { "RomPath",         &LauncherSettings::rom_path },
    { "SourceFolder",    &LauncherSettings::source_folder },
    { "NesrecompFolder", &LauncherSettings::nesrecomp_folder },
    { "RecompUiFolder",  &LauncherSettings::recomp_ui_folder },
    { "ToolsFolder",     &LauncherSettings::tools_folder },
    { "GameFolder",      &LauncherSettings::game_folder },
    { "BuiltRomSha1",    &LauncherSettings::built_rom_sha1 },
    { "BuiltVersion",    &LauncherSettings::built_version },
    { "VoiceChoice",     &LauncherSettings::voice_choice },
};
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
        for (const Key &k : kKeys)
            if (key == k.name) s.*k.field = value;
    }
    std::fclose(f);
    return s;
}

void launcher_settings_save(const LauncherSettings &s)
{
    FILE *f = utf8_fopen(ini_path().c_str(), "wb");
    if (!f) return;
    std::fprintf(f, "# Shadowgreat launcher settings.\n"
                    "# The folders are found automatically when left empty.\n");
    for (const Key &k : kKeys) std::fprintf(f, "%s = %s\n", k.name, (s.*k.field).c_str());
    std::fclose(f);
}
