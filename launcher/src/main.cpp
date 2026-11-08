// main.cpp - the Shadowgreat launcher (Phase 8).
//
// Milestone A: the window, picking the player's ROM, checking it is
// Shadowgate (USA), and remembering it. Later milestones fill in the build,
// the downloads (build tools; the optional narrator voice), the options and
// Play. The launcher contains no game data; it only stores a checksum.
//
// Dear ImGui over SDL2's renderer, the same toolkit as the in-game menu.

#include "launcher_settings.h"
#include "rom_check.h"

#include <SDL.h>
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

#include <cstdio>
#include <cstring>
#include <string>

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
};

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

void draw(State &st, SDL_Window *win)
{
    ImGuiIO &io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("##launcher", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);

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

    // 2-4: coming in the next milestones
    bool rom_ok = st.checked && st.check.ok;
    step_header(2, "Build the game", rom_ok);
    ImGui::Indent(); ImGui::TextColored(kMuted, "Coming next: builds Shadowgate.exe on this PC."); ImGui::Unindent();
    ImGui::Spacing();
    step_header(3, "Narrator voice (optional)", false);
    ImGui::Indent(); ImGui::TextColored(kMuted, "Coming soon: download the voice, or skip it."); ImGui::Unindent();
    ImGui::Spacing();
    step_header(4, "Options", false);
    ImGui::Indent(); ImGui::TextColored(kMuted, "Coming soon: mouse, hand pointer, narration."); ImGui::Unindent();

    // Play, bottom right
    const char *play = "Play";
    ImVec2 play_size(ImGui::CalcTextSize(play).x + 60.0f * io.FontGlobalScale + ImGui::GetStyle().FramePadding.x * 2,
                     ImGui::GetFrameHeight() * 1.4f);
    ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - play_size.x - ImGui::GetStyle().WindowPadding.x,
                               ImGui::GetWindowHeight() - play_size.y - ImGui::GetStyle().WindowPadding.y));
    ImGui::BeginDisabled(true);
    ImGui::Button(play, play_size);
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

    launcher_settings_save(st.settings);
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
