// game_config.cpp - see game_config.h.
#include "game_config.h"
#include "utf8_file.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>

static std::string trim(const std::string &s)
{
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

static bool same(const std::string &a, const std::string &b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
    return true;
}

static bool is_section(const std::string &line, std::string *name)
{
    std::string t = trim(line);
    if (t.size() < 2 || t.front() != '[' || t.back() != ']') return false;
    if (name) *name = t.substr(1, t.size() - 2);
    return true;
}

static bool key_of(const std::string &line, std::string *key)
{
    std::string t = trim(line);
    if (t.empty() || t[0] == '#' || t[0] == ';') return false;
    size_t eq = t.find('=');
    if (eq == std::string::npos) return false;
    *key = trim(t.substr(0, eq));
    return true;
}

bool IniFile::load(const std::string &path)
{
    path_ = path;
    lines_.clear();
    FILE *f = utf8_fopen(path.c_str(), "rb");
    if (!f) return false;
    std::string text;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
    std::fclose(f);
    size_t at = 0;
    while (at < text.size()) {
        size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(at, end - at);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines_.push_back(line);
        at = end + 1;
    }
    return true;
}

bool IniFile::save() const
{
    FILE *f = utf8_fopen(path_.c_str(), "wb");
    if (!f) return false;
    for (const std::string &l : lines_) { std::fputs(l.c_str(), f); std::fputc('\n', f); }
    return std::fclose(f) == 0;
}

std::string IniFile::get(const std::string &section, const std::string &key, const std::string &fallback) const
{
    std::string current, k;
    for (const std::string &l : lines_) {
        if (is_section(l, &current)) continue;
        if (same(current, section) && key_of(l, &k) && same(k, key))
            return trim(l.substr(l.find('=') + 1));
    }
    return fallback;
}

void IniFile::set(const std::string &section, const std::string &key, const std::string &value)
{
    std::string line = key + " = " + value, current, k;
    long last_in_section = -1;                    // where to add a missing key
    for (size_t i = 0; i < lines_.size(); ++i) {
        if (is_section(lines_[i], &current)) {
            if (same(current, section)) last_in_section = (long)i;
            continue;
        }
        if (!same(current, section)) continue;
        if (key_of(lines_[i], &k) && same(k, key)) { lines_[i] = line; return; }
        if (!trim(lines_[i]).empty()) last_in_section = (long)i;
    }
    if (last_in_section >= 0) {
        lines_.insert(lines_.begin() + last_in_section + 1, line);
    } else {
        lines_.push_back("[" + section + "]");
        lines_.push_back(line);
    }
}

// ---- the options ------------------------------------------------------------------
const char *const kVoiceNames[] = { "Narrator", "Grim", "Plain" };
const int kVoiceCount = 3;

static bool on(const std::string &v, bool fallback)
{
    if (same(v, "on") || v == "1" || same(v, "true") || same(v, "yes")) return true;
    if (same(v, "off") || v == "0" || same(v, "false") || same(v, "no")) return false;
    return fallback;
}

static int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static int seconds_to_ms(const std::string &v, int fallback)
{
    if (v.empty()) return fallback;
    char *end = nullptr;
    double s = std::strtod(v.c_str(), &end);
    if (end == v.c_str()) return fallback;
    return clamp((int)(s * 1000.0 + 0.5), 0, 2000);
}

GameOptions game_options_read(const std::string &config_path)
{
    GameOptions o, d;
    IniFile ini;
    ini.load(config_path);
    o.mouse = on(ini.get("Game", "Mouse", ""), d.mouse);
    o.hand = on(ini.get("Game", "HandFollowsMouse", ""), d.hand);
    o.narration = on(ini.get("Game", "Narration", ""), d.narration);
    std::string voice = ini.get("Game", "NarratorVoice", kVoiceNames[0]);
    for (int i = 0; i < kVoiceCount; ++i) if (same(voice, kVoiceNames[i])) o.voice = i;
    o.volume = clamp(std::atoi(ini.get("Game", "VoiceVolume", std::to_string(d.volume)).c_str()), 0, 100);
    o.music = clamp(std::atoi(ini.get("Game", "MusicWhileSpeaking", std::to_string(d.music)).c_str()), 0, 100);
    o.quote_ms = seconds_to_ms(ini.get("Game", "QuotePause", ""), d.quote_ms);
    o.exclaim_ms = seconds_to_ms(ini.get("Game", "ExclaimPause", ""), d.exclaim_ms);
    o.skip_start_screen = on(ini.get("Launcher", "SkipLauncher", ""), d.skip_start_screen);
    return o;
}

bool game_options_write(const std::string &config_path, const GameOptions &o)
{
    IniFile ini;
    ini.load(config_path);
    char pause[32];
    ini.set("Game", "Mouse", o.mouse ? "On" : "Off");
    ini.set("Game", "HandFollowsMouse", o.hand ? "On" : "Off");
    ini.set("Game", "Narration", o.narration ? "On" : "Off");
    ini.set("Game", "NarratorVoice", kVoiceNames[clamp(o.voice, 0, kVoiceCount - 1)]);
    ini.set("Game", "VoiceVolume", std::to_string(clamp(o.volume, 0, 100)));
    ini.set("Game", "MusicWhileSpeaking", std::to_string(clamp(o.music, 0, 100)));
    std::snprintf(pause, sizeof pause, "%.2f", clamp(o.quote_ms, 0, 2000) / 1000.0);
    ini.set("Game", "QuotePause", pause);
    std::snprintf(pause, sizeof pause, "%.2f", clamp(o.exclaim_ms, 0, 2000) / 1000.0);
    ini.set("Game", "ExclaimPause", pause);
    ini.set("Launcher", "SkipLauncher", o.skip_start_screen ? "1" : "0");
    return ini.save();
}
