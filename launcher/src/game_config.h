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
    // Each narrator's own voice, in hundredths: speed, depth, pause
    // ([Game] DungeonMasterSpeed etc.; as sg_voice.c's presets).
    int tune[3][3] = { { 122, 85, 50 }, { 112, 95, 60 }, { 100, 100, 60 } };
    bool torch = false;             // torch tracker
    bool pad_pointer = true;        // a pad stick moves the pointer
    int pointer_stick = 0;          // 0 = left, 1 = right
    int pointer_speed = 5;          // 1-10
    bool skip_start_screen = false;
};

extern const char *const kVoiceNames[];
extern const int kVoiceCount;
enum { kTuneSpeed, kTuneDepth, kTunePause };

// The Narrator Options back to normal, keeping the chosen narrator: its
// speed, depth and pause, and the pauses after quotes and "!".
void game_options_narrator_defaults(GameOptions &o);

GameOptions game_options_read(const std::string &config_path);
bool game_options_write(const std::string &config_path, const GameOptions &o);
