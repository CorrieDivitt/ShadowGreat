// main.cpp - the Shadowgreat launcher (Phase 8).
//
// Picks the player's ROM and checks it is Shadowgate (USA) (milestone A),
// builds the game on this PC and starts it (milestone B), and downloads the
// build tools and the optional narrator voice (milestone C), and sets the
// Shadowgreat options before playing (milestone D).
// The launcher contains no game data; it only stores a checksum.
//
// Dear ImGui over SDL2's renderer, the same toolkit as the in-game menu.

#include "build_job.h"
#include "game_config.h"
#include "launcher_settings.h"
#include "paths.h"
#include "child_process.h"
#include "rom_check.h"
#include "version.h"
#include "utf8_file.h"

#include <SDL.h>
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#else
#include "tinyfiledialogs.h"
#endif

namespace {

// ---- look ----------------------------------------------------------------
const ImVec4 kAccent   = ImVec4(0.86f, 0.24f, 0.20f, 1.0f);   // the menu's red title colour
const ImVec4 kGood     = ImVec4(0.45f, 0.80f, 0.45f, 1.0f);
const ImVec4 kBad      = ImVec4(0.95f, 0.55f, 0.35f, 1.0f);
const ImVec4 kMuted    = ImVec4(0.60f, 0.60f, 0.62f, 1.0f);
float g_scale = 1.0f;

void apply_style(float scale)
{
    ImGuiStyle &s = ImGui::GetStyle();
    ImGui::StyleColorsDark();
    s.WindowRounding = 8.0f;
    s.FrameRounding = 6.0f;
    s.ChildRounding = 8.0f;
    s.FramePadding = ImVec2(12.0f, 8.0f);
    s.ItemSpacing = ImVec2(10.0f, 10.0f);
    s.WindowPadding = ImVec2(22.0f, 20.0f);
    ImVec4 *c = s.Colors;
    c[ImGuiCol_WindowBg]       = ImVec4(0.11f, 0.11f, 0.12f, 1.0f);
    c[ImGuiCol_ChildBg]        = ImVec4(0.15f, 0.15f, 0.16f, 1.0f);
    c[ImGuiCol_Border]         = ImVec4(0.26f, 0.26f, 0.28f, 1.0f);
    c[ImGuiCol_FrameBg]        = ImVec4(0.20f, 0.20f, 0.22f, 1.0f);
    c[ImGuiCol_Button]         = ImVec4(0.24f, 0.24f, 0.26f, 1.0f);
    c[ImGuiCol_ButtonHovered]  = ImVec4(0.32f, 0.32f, 0.35f, 1.0f);
    c[ImGuiCol_ButtonActive]   = ImVec4(0.40f, 0.40f, 0.43f, 1.0f);
    c[ImGuiCol_Separator]      = ImVec4(0.26f, 0.26f, 0.28f, 1.0f);
    s.ScaleAllSizes(scale);
}

void load_font(float scale)
{
    ImGuiIO &io = ImGui::GetIO();
    std::string path;
    if (char *base = SDL_GetBasePath()) {
        path = std::string(base) + "assets/LatoLatin-Regular.ttf";
        SDL_free(base);
    }
    ImFontConfig cfg;
    cfg.OversampleH = 2;
    if (FILE *f = std::fopen(path.c_str(), "rb")) {
        std::fclose(f);
        io.Fonts->AddFontFromFileTTF(path.c_str(), 19.0f * scale, &cfg);
    } else {
        cfg.SizePixels = 15.0f * scale;
        io.Fonts->AddFontDefault(&cfg);
    }
}

// ---- picking a file ----------------------------------------------------------
std::string pick_rom_file(SDL_Window *win)
{
#ifdef _WIN32
    (void)win;
    wchar_t file[1024] = L"";
    OPENFILENAMEW ofn;
    std::memset(&ofn, 0, sizeof ofn);
    ofn.lStructSize = sizeof ofn;
    ofn.lpstrFilter = L"NES ROM (*.nes)\0*.nes\0All files (*.*)\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = 1024;
    ofn.lpstrTitle = L"Choose your Shadowgate .nes file";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return std::string();
    char utf8[4096];
    if (!WideCharToMultiByte(CP_UTF8, 0, file, -1, utf8, sizeof utf8, nullptr, nullptr)) return std::string();
    return std::string(utf8);
#else
    (void)win;
    const char *filter_patterns[] = { "*.nes" };
    const char *file = tinyfd_openFileDialog(
        "Choose your Shadowgate .nes file",
        "",
        1,
        filter_patterns,
        "NES ROM (*.nes)",
        0
    );
    return file ? std::string(file) : std::string();
#endif
}

// ---- the screen ----------------------------------------------------------------
struct State {
    LauncherSettings settings;
    RomCheck check;
    bool checked = false;
    Folders folders;
    Tools tools;
    Voice voice;
    bool built = false;             // shadowgate.exe is there
    BuildStage last_stage = BuildStage::Idle;
    bool show_details = false;
    size_t log_lines_seen = 0;
    std::string play_message;
    GameOptions options;            // as in the game's config.ini
    std::string options_message;    // couldn't save them
    bool game_was_running = false;
};

std::string config_path(const State &st) { return st.folders.game + "/config.ini"; }

// The game's own pre-game screen (nesrecomp) remembers the ROM in rom.cfg
// beside shadowgate.exe: one line, the path. Fill it in with the ROM chosen
// here so the player never has to pick it twice.
void write_rom_cfg(const State &st)
{
    std::string path = st.settings.rom_path;
    for (char &c : path) if (c == '/') c = '\\';
    if (FILE *f = utf8_fopen((st.folders.game + "/rom.cfg").c_str(), "w")) {
        std::fprintf(f, "%s\n", path.c_str());
        std::fclose(f);
    }
}

void look_around(State &st)
{
    st.folders = find_folders(st.settings);
    st.tools = find_tools(st.folders.tools);
    st.voice = find_voice(st.folders, st.check.ok ? st.check.region_name : "");
    st.built = file_exists(game_exe(st.folders));
    if (!game_running()) st.options = game_options_read(config_path(st));
}

void check_rom(State &st)
{
    st.checked = !st.settings.rom_path.empty();
    if (st.checked) st.check = rom_check(st.settings.rom_path.c_str());
}

void step_header(int number, const char *title, bool active)
{
    ImGui::PushStyleColor(ImGuiCol_Text, active ? kAccent : kMuted);
    ImGui::Text("%d", number);
    ImGui::PopStyleColor();
    ImGui::SameLine();
    if (!active) ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
    ImGui::TextUnformatted(title);
    if (!active) ImGui::PopStyleColor();
}

void draw_log(State &st)
{
    ImGui::BeginChild("##log", ImVec2(0, 220.0f * g_scale), ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_HorizontalScrollbar);
    build_with_log([&](const std::vector<std::string> &lines) {
        ImGuiListClipper clip;
        clip.Begin((int)lines.size());
        while (clip.Step())
            for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i)
                ImGui::TextUnformatted(lines[i].c_str());
        // Follow new output while scrolled to the bottom.
        if (lines.size() != st.log_lines_seen && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f)
            ImGui::SetScrollHereY(1.0f);
        st.log_lines_seen = lines.size();
    });
    ImGui::EndChild();
}

// Progress of the running job, with Cancel.
void draw_progress(const BuildProgress &p)
{
    char overlay[64] = "";
    bool counting = p.total > 0;
    if (counting && p.stage == BuildStage::Downloading)
        std::snprintf(overlay, sizeof overlay, "%.0f of %.0f MB", p.done / 1048576.0, p.total / 1048576.0);
    else if (counting && p.stage == BuildStage::Compiling)
        std::snprintf(overlay, sizeof overlay, "%lld of %lld", (long long)p.done, (long long)p.total);
    else if (counting && p.stage == BuildStage::Unpacking)
        std::snprintf(overlay, sizeof overlay, "%.0f%%", 100.0 * p.done / p.total);
    float fraction = counting ? (float)((double)p.done / (double)p.total) : -1.0f * (float)ImGui::GetTime();
    float cancel_w = ImGui::CalcTextSize("Cancel").x + ImGui::GetStyle().FramePadding.x * 2;
    ImGui::ProgressBar(fraction, ImVec2(ImGui::GetContentRegionAvail().x - cancel_w - ImGui::GetStyle().ItemSpacing.x, 0),
                       counting ? overlay : nullptr);
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) build_cancel();
    ImGui::TextColored(kMuted, "%s", p.message.c_str());
}

// Under the section whose job ran last: its result and the full output.
void draw_job_result(State &st, const BuildProgress &p, JobKind kind)
{
    if (p.kind != kind) return;
    if (!build_running() && (p.stage == BuildStage::Failed || p.stage == BuildStage::Cancelled))
        ImGui::TextColored(kBad, "%s", p.message.c_str());
    if (build_running() || p.stage != BuildStage::Idle) {
        ImGui::PushID((int)kind);
        ImGui::Checkbox("Show details", &st.show_details);
        if (st.show_details) draw_log(st);
        ImGui::PopID();
    }
}

// The game was built by another version of Shadowgreat or for a different ROM:
// it must be built again before playing.
bool needs_rebuild(const State &st)
{
    return st.built && (st.settings.built_version != SG_VERSION ||
                        (st.check.ok && st.settings.built_rom_sha1 != st.check.sha1_body));
}

void draw_build(State &st, bool rom_ok)
{
    BuildProgress p = build_progress();
    bool running = build_running();
    bool mine = p.kind == JobKind::Build || p.kind == JobKind::Tools;

    if (!st.folders.ok()) {
        ImGui::TextColored(kBad, "Can't find %s.", st.folders.missing.c_str());
        ImGui::TextColored(kMuted, "Their places can be set in launcher.ini beside the launcher.");
        return;
    }
    if (running && mine) {
        draw_progress(p);
    } else if (!st.tools.ok()) {
        ImGui::TextColored(kMuted, "The build tools are a one-time download of about %d MB:", st.tools.download_mb);
        ImGui::TextColored(kMuted, "%s.", st.tools.missing.c_str());
        ImGui::BeginDisabled(running);
        if (ImGui::Button("Download the build tools")) {
            look_around(st);
            if (!st.tools.ok()) fetch_tools_start(st.folders, st.tools);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextColored(kMuted, "Into %s", st.folders.tools.c_str());
    } else if (!rom_ok) {
        ImGui::TextColored(kMuted, "Choose your ROM first.");
    } else {
        ImGui::BeginDisabled(running);
        if (ImGui::Button(st.built ? "Rebuild" : "Build")) {
            look_around(st);
            if (st.folders.ok() && st.tools.ok()) {
                st.play_message.clear();
                build_start(st.folders, st.tools, st.settings.rom_path);
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (needs_rebuild(st))
            ImGui::TextColored(kBad, "Shadowgreat was updated to %s. Press Rebuild to build the new version "
                                     "(your saves and settings are kept).", SG_VERSION);
        else if (st.built)
            ImGui::TextColored(kGood, "%s", p.kind == JobKind::Build && p.stage == BuildStage::Finished
                                                 ? p.message.c_str() : "Built and ready to play.");
        else if (p.kind == JobKind::Tools && p.stage == BuildStage::Finished)
            ImGui::TextColored(kGood, "The build tools are ready. Now build the game (a few minutes, only once).");
        else
            ImGui::TextColored(kMuted, "Builds shadowgate.exe on this PC (a few minutes; only needed once).");
    }
    draw_job_result(st, p, p.kind == JobKind::Tools ? JobKind::Tools : JobKind::Build);
}

// Remove the downloaded voice (the DLLs and the model), nothing else.
bool remove_voice(const Voice &v)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    bool ok = true;
    for (const char *dll : { "onnxruntime.dll", "onnxruntime_providers_shared.dll",
                             "sherpa-onnx-c-api.dll", "sherpa-onnx-cxx-api.dll",
                             "libonnxruntime.so", "libsherpa-onnx-c-api.so", "libsherpa-onnx-cxx-api.so",
                             "lexicon-fr.txt", "lexicon-de.txt", "lexicon-sv.txt" }) {
        fs::path f = fs::u8path(v.folder + "/" + dll);
        if (fs::exists(f, ec) && !fs::remove(f, ec)) ok = false;
    }
    fs::remove_all(fs::u8path(v.folder + "/kokoro-multi-lang-v1_0"), ec);
    if (ec) ok = false;
    return ok;
}

void draw_voice(State &st)
{
    BuildProgress p = build_progress();
    bool running = build_running();
    static std::string s_remove_message;

    if (running && p.kind == JobKind::Voice) {
        draw_progress(p);
    } else if (st.voice.installed()) {
        ImGui::TextColored(kGood, "Installed. The narrator reads the game's messages aloud.");
        ImGui::SameLine();
        ImGui::BeginDisabled(running);
        if (ImGui::SmallButton("Remove voice files")) {
            s_remove_message = remove_voice(st.voice) ? "" : "Couldn't remove all of it. Is the game still running?";
            st.settings.voice_choice = "skip";
            launcher_settings_save(st.settings);
            look_around(st);
        }
        ImGui::EndDisabled();
    } else {
        if (st.settings.voice_choice == "skip")
            ImGui::TextColored(kMuted, "Skipped: the game plays without narration. You can add the voice any time.");
        else
            ImGui::TextColored(kMuted, "A narrator can read the message box aloud. It's a separate download of "
                                       "about %d MB.", st.voice.download_mb);
        ImGui::BeginDisabled(running);
        if (ImGui::Button("Download voice files")) {
            look_around(st);
            st.settings.voice_choice.clear();
            launcher_settings_save(st.settings);
            if (!st.voice.installed()) fetch_voice_start(st.folders, st.tools, st.voice);
        }
        if (st.settings.voice_choice != "skip") {
            ImGui::SameLine();
            if (ImGui::Button("Play without voice")) {
                st.settings.voice_choice = "skip";
                launcher_settings_save(st.settings);
            }
        }
        ImGui::EndDisabled();
    }
    if (!s_remove_message.empty()) ImGui::TextColored(kBad, "%s", s_remove_message.c_str());
    draw_job_result(st, p, JobKind::Voice);
}

// A job just ended: remember a build, and look again at what's installed.
void job_ended(State &st)
{
    static bool s_was_running = false;
    BuildProgress p = build_progress();
    bool running = build_running();
    if (running) { s_was_running = true; return; }
    if (!s_was_running && p.stage == st.last_stage) return;
    s_was_running = false;
    if (p.kind == JobKind::Build && p.stage == BuildStage::Finished) {
        st.settings.built_rom_sha1 = st.check.sha1_body;
        st.settings.built_version = SG_VERSION;
        launcher_settings_save(st.settings);
    }
    if (p.stage == BuildStage::Failed) st.show_details = true;
    look_around(st);
    st.last_stage = p.stage;
}

// ---- 4. Options ---------------------------------------------------------------------
// The same Shadowgreat options as the in-game menu (Esc), saved straight into
// the game's config.ini. While the game is open they're changed there instead
// (the game saves its own copy when it closes), so here they wait.
void option_row(const char *label, const char *help)
{
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    if (help && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", help);
    ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(-FLT_MIN);
}

// A slider whose new value is kept when it's let go.
bool slider(const char *id, int *v, int lo, int hi, const char *format, int step)
{
    ImGui::SliderInt(id, v, lo, hi, format, ImGuiSliderFlags_AlwaysClamp);
    if (step > 1) *v = (*v + step / 2) / step * step;
    return ImGui::IsItemDeactivatedAfterEdit();
}

// A heading row, as in the in-game menu ("" = just a gap).
void heading_row(const char *title)
{
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::Dummy(ImVec2(0.0f, 4.0f * g_scale));
    if (title[0]) ImGui::TextColored(kAccent, "%s", title);
}

// A Speed / Depth / Pause slider: kept in hundredths, shown as 1.22. A value
// that's off the steps (Dungeon Master's 1.22) is kept until it's moved.
bool tune_slider(const char *id, int *v, int lo, int hi, const char *format, int step)
{
    float f = *v / 100.0f;
    if (ImGui::SliderFloat(id, &f, lo / 100.0f, hi / 100.0f, format, ImGuiSliderFlags_AlwaysClamp)) {
        int n = (int)(f * 100.0f + 0.5f);
        *v = (n + step / 2) / step * step;
    }
    return ImGui::IsItemDeactivatedAfterEdit();
}

void draw_options(State &st)
{
    bool playing = game_running();
    if (st.game_was_running && !playing) {
        look_around(st);                                       // the game may have changed them
        unsigned long code = 0;
        if (game_closed_at_once(&code)) {
            char text[512];
#ifdef _WIN32
            std::snprintf(text, sizeof text,
                          "The game closed straight away (exit code 0x%08lX). Its log is %s/shadowgate.log; "
                          "also check Windows Security > Protection history.", code, st.folders.game.c_str());
#else
            std::snprintf(text, sizeof text,
                          "The game closed straight away (exit code 0x%08lX). Its log is %s/shadowgate.log.",
                          code, st.folders.game.c_str());
#endif
            st.play_message = text;
        }
    }
    st.game_was_running = playing;

    if (!st.built) {
        ImGui::TextColored(kMuted, "Build the game first.");
        return;
    }
    if (playing)
        ImGui::TextColored(kMuted, "The game is open: change these in its menu (Esc). They show here when it closes.");

    GameOptions &o = st.options;
    bool changed = false;
    bool voice = st.voice.installed();
    ImGui::BeginDisabled(playing);
    if (ImGui::BeginTable("##options", 2, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, 300.0f * g_scale);
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);

        heading_row("Control Options");
        option_row("Mouse control", "Left click selects, right click is B.");
        changed |= ImGui::Checkbox("##mouse", &o.mouse);
        option_row("Hand follows the mouse", "The game's hand is the pointer.");
        ImGui::BeginDisabled(!o.mouse);
        changed |= ImGui::Checkbox("##hand", &o.hand);
        ImGui::EndDisabled();
        option_row("Analog stick as mouse", "Point with analog stick; A or pressing a stick in clicks where it points.");
        ImGui::BeginDisabled(!o.mouse);
        changed |= ImGui::Checkbox("##pad", &o.pad_pointer);
        ImGui::EndDisabled();
        ImGui::BeginDisabled(!o.mouse || !o.pad_pointer);
        option_row("Analog stick", "Which stick moves the pointer. The left stick then stops working the D-pad.");
        {
            static const char *const sticks[] = { "Left stick", "Right stick" };
            if (ImGui::BeginCombo("##stick", sticks[o.pointer_stick ? 1 : 0])) {
                for (int i = 0; i < 2; ++i)
                    if (ImGui::Selectable(sticks[i], i == o.pointer_stick)) { o.pointer_stick = i; changed = true; }
                ImGui::EndCombo();
            }
        }
        option_row("Pointer speed", "How fast the stick moves the pointer.");
        changed |= slider("##speed", &o.pointer_speed, 1, 10, "%d", 1);
        ImGui::EndDisabled();

        heading_row("Sound Options");
        ImGui::BeginDisabled(!voice || !o.narration);
        option_row("Voice volume", "How loud the narrator is.");
        changed |= slider("##volume", &o.volume, 0, 100, "%d", 5);
        option_row("Music volume during narration", "Game sound during speech.");
        changed |= slider("##music", &o.music, 0, 100, "%d", 5);
        ImGui::EndDisabled();

        heading_row("Narrator Options");
        option_row("Narrator", "Reads the message box aloud.");
        ImGui::BeginDisabled(!voice);
        bool narration = o.narration && voice;   // shown Off without the voice files
        if (ImGui::Checkbox("##narration", &narration)) { o.narration = narration; changed = true; }
        ImGui::EndDisabled();
        if (!voice) { ImGui::SameLine(); ImGui::TextColored(kMuted, "Needs the voice files (step 3)."); }

        ImGui::BeginDisabled(!voice || !o.narration);
        option_row("Narration voice", "A new voice starts with the next message.");
        if (ImGui::BeginCombo("##voice", kVoiceNames[o.voice])) {
            for (int i = 0; i < kVoiceCount; ++i)
                if (ImGui::Selectable(kVoiceNames[i], i == o.voice)) { o.voice = i; changed = true; }
            ImGui::EndCombo();
        }
        option_row("Pause after quotations", "Quiet after a closing quote.");
        changed |= slider("##quote", &o.quote_ms, 0, 2000, "%d ms", 100);
        option_row("Pause after \"!\"", "Quiet after an exclamation mark.");
        changed |= slider("##exclaim", &o.exclaim_ms, 0, 2000, "%d ms", 100);
        int *tune = o.tune[o.voice];                // this narrator's own voice
        option_row("Speed", "Speed of narrator speech");
        changed |= tune_slider("##vspeed", &tune[kTuneSpeed], 50, 150, "%.2f", 5);
        option_row("Depth", "Depth of narrator voice");
        changed |= tune_slider("##vdepth", &tune[kTuneDepth], 50, 150, "%.2f", 5);
        option_row("Pause", "Pause length");
        changed |= tune_slider("##vpause", &tune[kTunePause], 0, 200, "%.1f", 10);
        option_row("Narrator defaults", "Restore default narrator voice");
        if (ImGui::Button("Restore Defaults##narrator")) { game_options_narrator_defaults(o); changed = true; }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Restore default narrator voice");
        ImGui::EndDisabled();

        heading_row("");
        option_row("Torch tracker", "Show remaining torch life: orange at 15, red at 7.");
        changed |= ImGui::Checkbox("##torch", &o.torch);

        option_row("Skip the game's start screen",
                   "Play goes straight into the game. The start screen has the display and controller settings.");
        changed |= ImGui::Checkbox("##skip", &o.skip_start_screen);
        ImGui::EndTable();
    }
    if (ImGui::Button("Restore Defaults")) {
        GameOptions d;
        d.skip_start_screen = o.skip_start_screen;   // not one of the Shadowgreat options
        o = d;
        changed = true;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Restore Shadowgreat default options");
    ImGui::EndDisabled();

    if (changed && !playing)
        st.options_message = game_options_write(config_path(st), o) ? "" : "Couldn't save the options to config.ini.";
    if (!st.options_message.empty()) ImGui::TextColored(kBad, "%s", st.options_message.c_str());
}

void draw(State &st, SDL_Window *win)
{
    ImGuiIO &io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("##launcher", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);

    ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
    ImGui::SetWindowFontScale(1.6f);
    ImGui::TextUnformatted("Shadowgreat");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleColor();
#ifdef _WIN32
    ImGui::TextColored(kMuted, "Shadowgate for Windows, built on your PC from your own ROM.  Version %s", SG_VERSION);
#else
    ImGui::TextColored(kMuted, "Shadowgate for Linux, built on your PC from your own ROM.  Version %s", SG_VERSION);
#endif
    ImGui::Separator();
    ImGui::Spacing();

    // 1. The ROM
    step_header(1, "Your Shadowgate ROM", true);
    ImGui::Indent();
    const char *shown = st.settings.rom_path.empty() ? "(none chosen yet)" : st.settings.rom_path.c_str();
    float browse_w = ImGui::CalcTextSize("Browse...").x + ImGui::GetStyle().FramePadding.x * 2;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - browse_w - ImGui::GetStyle().ItemSpacing.x);
    ImGui::BeginDisabled();
    char buf[1024];
    std::snprintf(buf, sizeof buf, "%s", shown);
    ImGui::InputText("##rom", buf, sizeof buf, ImGuiInputTextFlags_ReadOnly);
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Browse...")) {
        std::string path = pick_rom_file(win);
        if (!path.empty()) {
            st.settings.rom_path = path;
            check_rom(st);
            look_around(st);
            launcher_settings_save(st.settings);
        }
    }
    if (st.checked) ImGui::TextColored(st.check.ok ? kGood : kBad, "%s", st.check.message);
    else ImGui::TextColored(kMuted, "Choose your legally obtained Shadowgate .nes file (USA, Europe, Sweden, France, Germany).");
    ImGui::Unindent();
    ImGui::Spacing();

    job_ended(st);
    bool rom_ok = st.checked && st.check.ok;
    step_header(2, "Build the game", rom_ok);
    ImGui::Indent();
    draw_build(st, rom_ok);
    ImGui::Unindent();
    ImGui::Spacing();
    step_header(3, "Narrator voice (optional)", true);
    ImGui::Indent(); draw_voice(st); ImGui::Unindent();
    ImGui::Spacing();
    step_header(4, "Options", st.built);
    ImGui::Indent(); draw_options(st); ImGui::Unindent();

    // Play, bottom right (below everything when the window is short)
    bool playing = game_running();
    const char *play = playing ? "Playing..." : "Play";
    const ImGuiStyle &style = ImGui::GetStyle();
    ImVec2 play_size(ImGui::CalcTextSize(play).x + 60.0f * g_scale + style.FramePadding.x * 2,
                     ImGui::GetFrameHeight() * 1.4f);
    float bottom = ImGui::GetWindowHeight() - play_size.y - style.WindowPadding.y;
    float row = ImGui::GetCursorPosY() > bottom ? ImGui::GetCursorPosY() : bottom;
    if (!st.play_message.empty()) {           // beside Play, wrapping before it
        ImGui::SetCursorPosY(row);
        ImGui::PushTextWrapPos(ImGui::GetWindowWidth() - play_size.x - style.WindowPadding.x - style.ItemSpacing.x * 2);
        ImGui::PushStyleColor(ImGuiCol_Text, kBad);
        ImGui::TextWrapped("%s", st.play_message.c_str());
        ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();
    }
    ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - play_size.x - style.WindowPadding.x, row));
    bool can_play = rom_ok && st.built && !needs_rebuild(st) && !build_running() && !playing;
    ImGui::BeginDisabled(!can_play);
    ImGui::PushStyleColor(ImGuiCol_Button, can_play ? ImVec4(0.62f, 0.17f, 0.14f, 1.0f) : style.Colors[ImGuiCol_Button]);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.74f, 0.22f, 0.18f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.84f, 0.26f, 0.21f, 1.0f));
    if (ImGui::Button(play, play_size)) {
        write_rom_cfg(st);
        // The ROM goes on the command line too: with the start screen
        // skipped, the game doesn't read rom.cfg and would have no ROM.
        st.play_message = start_program(game_exe(st.folders), quote_arg(st.settings.rom_path), st.folders.game)
                        ? std::string() : "Couldn't start shadowgate.exe.";
    }
    ImGui::PopStyleColor(3);
    ImGui::EndDisabled();

    ImGui::End();
}

} // namespace

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    float dpi = 96.0f;
    if (SDL_GetDisplayDPI(0, &dpi, nullptr, nullptr) != 0 || dpi < 48.0f) dpi = 96.0f;
    float scale = dpi / 96.0f;
    g_scale = scale;
    int width = (int)(900 * scale), height = (int)(820 * scale);
    SDL_Rect usable;                          // fit on small screens (the page scrolls)
    if (SDL_GetDisplayUsableBounds(0, &usable) == 0) {
        if (width > usable.w * 9 / 10) width = usable.w * 9 / 10;
        if (height > usable.h * 9 / 10) height = usable.h * 9 / 10;
    }
    SDL_Window *win = SDL_CreateWindow("Shadowgreat", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       width, height,
                                       SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Renderer *ren = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED) : nullptr;
    if (!ren) {
        std::fprintf(stderr, "window: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetWindowMinimumSize(win, width < (int)(640 * scale) ? width : (int)(640 * scale),
                             height < (int)(480 * scale) ? height : (int)(480 * scale));

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    apply_style(scale);
    load_font(scale);
    ImGui_ImplSDL2_InitForSDLRenderer(win, ren);
    ImGui_ImplSDLRenderer2_Init(ren);

    State st;
    st.settings = launcher_settings_load();
    check_rom(st);
    look_around(st);

    bool running = true;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL2_ProcessEvent(&e);
            if (e.type == SDL_QUIT) running = false;
            if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_CLOSE) running = false;
        }
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        draw(st, win);
        ImGui::Render();
        SDL_SetRenderDrawColor(ren, 28, 28, 31, 255);
        SDL_RenderClear(ren);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), ren);
        SDL_RenderPresent(ren);
    }

    build_shutdown();
    launcher_settings_save(st.settings);
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
