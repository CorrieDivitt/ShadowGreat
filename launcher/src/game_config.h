// game_config.h - the game's config.ini: reading and changing a few keys
// while leaving every other line exactly as the game wrote it.
#pragma once
#include <string>
#include <vector>

class IniFile {
public:
    bool load(const std::string &path);          // false if it isn't there yet (that's fine)
    bool save() const;
    std::string get(const std::string &section, const std::string &key, const std::string &fallback) const;
    void set(const std::string &section, const std::string &key, const std::string &value);
private:
    std::string path_;
    std::vector<std::string> lines_;
};

// The Shadowgreat options (config.ini [Game]) and the start-screen switch
// ([Launcher] SkipLauncher). Defaults match src/features/feat_narration.c
// and the feature switches.
struct GameOptions {
    bool mouse = true;
    bool hand = true;
    bool narration = true;
    int voice = 0;                  // index into kVoiceNames
    int volume = 100;               // 0-100
    int music = 40;                 // 0-100: game sound while the narrator talks
    int quote_ms = 800;             // pause after a closing quote
    int exclaim_ms = 600;           // pause after "!"
    bool skip_start_screen = false;
};

extern const char *const kVoiceNames[];
extern const int kVoiceCount;

GameOptions game_options_read(const std::string &config_path);
bool game_options_write(const std::string &config_path, const GameOptions &o);
