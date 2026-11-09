// main.cpp - the Shadowgreat launcher (Phase 8).
//
// Picks the player's ROM and checks it is Shadowgate (USA) (milestone A),
// builds the game on this PC and starts it (milestone B), and downloads the
// build tools and the optional narrator voice (milestone C). Still to come:
// the options.
// The launcher contains no game data; it only stores a checksum.
//
// Dear ImGui over SDL2's renderer, the same toolkit as the in-game menu.

#include "build_job.h"
#include "launcher_settings.h"
#include "paths.h"
#include "child_process.h"
#include "rom_check.h"
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
    ofn.lpstrTitle = L"Choose your Shadowgate (USA) .nes file";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return std::string();
    char utf8[4096];
    if (!WideCharToMultiByte(CP_UTF8, 0, file, -1, utf8, sizeof utf8, nullptr, nullptr)) return std::string();
    return std::string(utf8);
#else
    (void)win;
    return std::string();
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
};

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
    st.voice = find_voice(st.folders);
    st.built = file_exists(game_exe(st.folders));
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
        if (st.built)
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
                             "sherpa-onnx-c-api.dll", "sherpa-onnx-cxx-api.dll" }) {
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
    BuildProgress p = build_progress();
    bool running = build_running();
    if (p.stage == st.last_stage) return;
    if (!running) {
        if (p.kind == JobKind::Build && p.stage == BuildStage::Finished) {
            st.settings.built_rom_sha1 = st.check.sha1_body;
            launcher_settings_save(st.settings);
        }
        if (p.stage == BuildStage::Failed) st.show_details = true;
        look_around(st);
    }
    st.last_stage = p.stage;
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
    ImGui::TextColored(kMuted, "Shadowgate for Windows, built on your PC from your own ROM.");
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
            launcher_settings_save(st.settings);
        }
    }
    if (st.checked) ImGui::TextColored(st.check.ok ? kGood : kBad, "%s", st.check.message);
    else ImGui::TextColored(kMuted, "Choose your legally obtained Shadowgate (USA) .nes file.");
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
    step_header(4, "Options", false);
    ImGui::Indent(); ImGui::TextColored(kMuted, "Coming soon: mouse, hand pointer, narration."); ImGui::Unindent();

    // Play, bottom right (below everything when the window is short)
    const char *play = "Play";
    const ImGuiStyle &style = ImGui::GetStyle();
    ImVec2 play_size(ImGui::CalcTextSize(play).x + 60.0f * g_scale + style.FramePadding.x * 2,
                     ImGui::GetFrameHeight() * 1.4f);
    float bottom = ImGui::GetWindowHeight() - play_size.y - style.WindowPadding.y;
    if (!st.play_message.empty()) {
        float y = bottom + (play_size.y - ImGui::GetTextLineHeight()) * 0.5f;
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() > y ? ImGui::GetCursorPosY() : y);
        ImGui::TextColored(kBad, "%s", st.play_message.c_str());
        ImGui::SameLine();
    }
    ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - play_size.x - style.WindowPadding.x,
                               ImGui::GetCursorPosY() > bottom ? ImGui::GetCursorPosY() : bottom));
    bool can_play = rom_ok && st.built && !build_running();
    ImGui::BeginDisabled(!can_play);
    ImGui::PushStyleColor(ImGuiCol_Button, can_play ? ImVec4(0.62f, 0.17f, 0.14f, 1.0f) : style.Colors[ImGuiCol_Button]);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.74f, 0.22f, 0.18f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.84f, 0.26f, 0.21f, 1.0f));
    if (ImGui::Button(play, play_size)) {
        write_rom_cfg(st);
        st.play_message = start_program(game_exe(st.folders), st.folders.game)
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
    SDL_Window *win = SDL_CreateWindow("Shadowgreat", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       (int)(860 * scale), (int)(600 * scale),
                                       SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Renderer *ren = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED) : nullptr;
    if (!ren) {
        std::fprintf(stderr, "window: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetWindowMinimumSize(win, (int)(640 * scale), (int)(480 * scale));

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
