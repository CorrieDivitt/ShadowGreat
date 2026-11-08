// paths.cpp - where things are.
//
// Two layouts are understood, so the same launcher works while developing
// and once packaged:
//   packaged:   Shadowgreat/ShadowgreatLauncher.exe, source/, nesrecomp/,
//               recomp-ui/, tools/, game/
//   developing: F:/NESDev/ShadowgatePort/build-launcher/ShadowgreatLauncher.exe,
//               with nesrecomp, recomp-ui and Tools beside ShadowgatePort.
// Any folder can also be set in launcher.ini.
#include "paths.h"

#include <SDL.h>
#include <filesystem>
#include <initializer_list>
#include <system_error>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace fs = std::filesystem;

static fs::path P(const std::string &s) { return fs::u8path(s); }

static std::string S(const fs::path &p)
{
    std::string s = p.lexically_normal().u8string();
    for (char &c : s) if (c == '\\') c = '/';
    while (s.size() > 3 && s.back() == '/') s.pop_back();
    return s;
}

bool file_exists(const std::string &utf8)
{
    std::error_code ec;
    return !utf8.empty() && fs::exists(P(utf8), ec);
}

std::string exe_folder()
{
    std::string r = "./";
    if (char *b = SDL_GetBasePath()) { r = b; SDL_free(b); }
    return S(P(r));
}

template <class Test>
static std::string first_of(std::initializer_list<std::string> candidates, Test test)
{
    for (const std::string &c : candidates) if (test(c)) return S(P(c));
    return std::string();
}

static std::string parent(const std::string &dir) { return S(P(dir).parent_path()); }

Folders find_folders(const LauncherSettings &s)
{
    Folders f;
    std::string exe = exe_folder();
    auto is_source = [](const std::string &d) {
        return file_exists(d + "/CMakeLists.txt") && file_exists(d + "/shadowgate-map.toml");
    };
    f.source = !s.source_folder.empty() ? s.source_folder
             : first_of({ exe + "/source", exe + "/..", exe }, is_source);
    std::string base = f.source.empty() ? exe : parent(f.source);

    auto is_nesrecomp = [](const std::string &d) { return file_exists(d + "/runner/cyc/project.cmake"); };
    f.nesrecomp = !s.nesrecomp_folder.empty() ? s.nesrecomp_folder
                : first_of({ exe + "/nesrecomp", base + "/nesrecomp" }, is_nesrecomp);

    auto is_recomp_ui = [](const std::string &d) { return file_exists(d + "/src/third_party/imgui/imgui.cpp"); };
    f.recomp_ui = !s.recomp_ui_folder.empty() ? s.recomp_ui_folder
                : first_of({ exe + "/recomp-ui", base + "/recomp-ui" }, is_recomp_ui);

    auto is_dir = [](const std::string &d) { return file_exists(d); };
    f.tools = !s.tools_folder.empty() ? s.tools_folder
            : first_of({ exe + "/tools", base + "/Tools" }, is_dir);

    f.game = !s.game_folder.empty() ? s.game_folder : exe + "/game";

    auto add = [&f](const char *what) { f.missing += f.missing.empty() ? what : std::string(", ") + what; };
    if (!is_source(f.source))       add("the Shadowgreat source");
    if (!is_nesrecomp(f.nesrecomp)) add("nesrecomp");
    if (!is_recomp_ui(f.recomp_ui)) add("recomp-ui");
    if (f.tools.empty())            add("the tools folder");
    return f;
}

// The first folder in `dir` whose name starts with `prefix` and holds `inside`.
static std::string find_in(const std::string &dir, const char *prefix, const char *inside)
{
    std::error_code ec;
    if (dir.empty()) return std::string();
    for (fs::directory_iterator it(P(dir), ec), end; !ec && it != end; it.increment(ec)) {
        std::string name = it->path().filename().u8string();
        if (name.compare(0, std::string(prefix).size(), prefix) != 0) continue;
        std::string candidate = S(it->path()) + "/" + inside;
        if (file_exists(candidate)) return S(P(candidate));
    }
    return std::string();
}

static std::string on_path(const char *program)
{
#ifdef _WIN32
    wchar_t wname[64], out[MAX_PATH];
    MultiByteToWideChar(CP_UTF8, 0, program, -1, wname, 64);
    if (!SearchPathW(nullptr, wname, nullptr, MAX_PATH, out, nullptr)) return std::string();
    char utf8[MAX_PATH * 4];
    WideCharToMultiByte(CP_UTF8, 0, out, -1, utf8, sizeof utf8, nullptr, nullptr);
    return S(P(utf8));
#else
    (void)program;
    return std::string();
#endif
}

Tools find_tools(const std::string &tools)
{
    Tools t;
    t.clang = find_in(tools, "llvm-mingw", "bin/clang.exe");
    t.clangxx = find_in(tools, "llvm-mingw", "bin/clang++.exe");
    t.ninja = file_exists(tools + "/ninja/ninja.exe") ? tools + "/ninja/ninja.exe"
            : file_exists(tools + "/ninja.exe") ? tools + "/ninja.exe" : std::string();
    t.sdl2_dir = find_in(tools, "SDL2-", "x86_64-w64-mingw32/lib/cmake/SDL2");
    t.cmake = find_in(tools, "cmake", "bin/cmake.exe");
    if (t.cmake.empty()) t.cmake = on_path("cmake.exe");

    auto add = [&t](const char *what) { t.missing += t.missing.empty() ? what : std::string(", ") + what; };
    if (t.clang.empty() || t.clangxx.empty()) add("the compiler (llvm-mingw)");
    if (t.ninja.empty())    add("Ninja");
    if (t.sdl2_dir.empty()) add("SDL2");
    if (t.cmake.empty())    add("CMake");
    return t;
}

std::string game_exe(const Folders &f) { return f.game + "/shadowgate.exe"; }
