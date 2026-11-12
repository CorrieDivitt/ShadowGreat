// build_job.cpp - see build_job.h.
//
// Building: the same two steps as building by hand in the tools window,
//   cmake -S <source> -B <game> -G Ninja ... -DSHADOWGATE_ROM=<your ROM>
//   cmake --build <game>
// Everything they write (including the code made from the ROM) goes in the
// game folder on this PC.
//
// Downloading: Python first, by the launcher itself (WinHTTP, and its own
// .tar.gz unpacking; no other program is started for it), checked against
// its SHA-256; then fetch.py downloads, checks and unpacks everything else. Output is saved to launcher-build.log beside the launcher.
#include "build_job.h"
#include "child_process.h"
#include "http_download.h"
#include "untar.h"
#include "utf8_file.h"

#include <SDL.h>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace {

// Python for the launcher (python-build-standalone, PSF licence).
const char *kPythonUrl =
    "https://github.com/astral-sh/python-build-standalone/releases/download/20260924/"
    "cpython-3.13.15+20260924-x86_64-pc-windows-msvc-install_only_stripped.tar.gz";
const char *kPythonSha256 = "e42fa944748a50e9ff481cbb817ef8a6e3da6fbcf0cf6f29b554e1acb8c7384d";

SDL_mutex *s_lock;
SDL_Thread *s_thread;
bool s_running;
BuildProgress s_progress;
std::vector<std::string> s_log;
FILE *s_log_file;
std::string s_error_line;        // fetch.py's "@error ..." message

struct Job { JobKind kind; Folders f; Tools t; Voice v; std::string rom; };
Job s_job;

const size_t kMaxLines = 20000;

struct Locked {
    Locked() { SDL_LockMutex(s_lock); }
    ~Locked() { SDL_UnlockMutex(s_lock); }
};

void set_stage(BuildStage stage, const std::string &message)
{
    Locked l;
    s_progress.stage = stage;
    s_progress.message = message;
    s_progress.done = s_progress.total = 0;
}

// One line of output: keep it, save it, and read the progress in it
// (Ninja's "[12/345]", fetch.py's "@..." lines).
void add_line(const std::string &line)
{
    long long a = 0, b = 0;
    bool counter = line.size() > 4 && line[0] == '[' && std::sscanf(line.c_str(), "[%lld/%lld]", &a, &b) == 2;
    if (line.compare(0, 7, "@bytes ") == 0 && std::sscanf(line.c_str() + 7, "%lld %lld", &a, &b) == 2) {
        Locked l;
        s_progress.done = a; s_progress.total = b;
        return;                                  // too many to log
    }
    if (s_log_file) { std::fputs(line.c_str(), s_log_file); std::fputc('\n', s_log_file); std::fflush(s_log_file); }
    Locked l;
    if (s_log.size() >= kMaxLines) s_log.erase(s_log.begin(), s_log.begin() + kMaxLines / 10);
    s_log.push_back(line);
    if (counter && b > 0) { s_progress.done = a; s_progress.total = b; }
    if (line.compare(0, 6, "@step ") == 0) {
        int n = 0, count = 0, used = 0;
        if (std::sscanf(line.c_str() + 6, "%d %d %n", &n, &count, &used) >= 2) {
            char text[256];
            std::snprintf(text, sizeof text, "Downloading %s (%d of %d)...", line.c_str() + 6 + used, n, count);
            s_progress.stage = BuildStage::Downloading;
            s_progress.message = text;
            s_progress.done = s_progress.total = 0;
        }
    } else if (line.compare(0, 8, "@unpack ") == 0 && std::sscanf(line.c_str() + 8, "%lld %lld", &a, &b) == 2) {
        if (s_progress.stage != BuildStage::Unpacking) {
            std::string m = s_progress.message;
            if (m.compare(0, 12, "Downloading ") == 0) m = "Unpacking " + m.substr(12);
            s_progress.message = m;
        }
        s_progress.stage = BuildStage::Unpacking;
        s_progress.done = a; s_progress.total = b;
    } else if (line.compare(0, 7, "@error ") == 0) {
        s_error_line = line.substr(7);
    }
}

bool contains(const std::string &s, const char *what) { return s.find(what) != std::string::npos; }

// A short reason for the player, picked out of the build output.
std::string why_it_failed(const char *step)
{
    Locked l;
    std::string reason;
    for (size_t i = 0; i < s_log.size(); ++i) {
        const std::string &line = s_log[i];
        if (contains(line, "Could NOT find Python3")) {
            reason = "Python 3.11 or newer is needed and was not found.";
            break;
        }
        if (contains(line, "CMake Error")) {
            for (size_t j = i + 1; j < s_log.size() && j < i + 6; ++j)
                if (s_log[j].find_first_not_of(' ') != std::string::npos) {
                    reason = s_log[j].substr(s_log[j].find_first_not_of(' '));
                    break;
                }
            break;
        }
        if (contains(line, "error:") || contains(line, "FAILED:")) { reason = line; break; }
    }
    std::string msg = std::string(step) + " failed.";
    if (!reason.empty()) msg += "\n" + reason;
    return msg + "\nShow the details below for the full output (also saved in launcher-build.log).";
}

std::string folder_of(const std::string &file)
{
    size_t at = file.find_last_of('/');
    return at == std::string::npos ? std::string() : file.substr(0, at);
}

int run(const std::string &command, const std::string &where, std::string *error)
{
    add_line("> " + command);
    return run_process(command, where, add_line, error);
}

void finish(int rc, const std::string &error, const char *what_failed, const char *done_message)
{
    if (rc == -2)      set_stage(BuildStage::Cancelled, "Cancelled. You can start it again any time.");
    else if (rc == -1) set_stage(BuildStage::Failed, "Couldn't start a program it needs: " + error);
    else if (rc != 0)  set_stage(BuildStage::Failed, what_failed);
    else               set_stage(BuildStage::Finished, done_message);
}

// ---- building ----------------------------------------------------------------
int do_build(const Job &j, std::string *error)
{
    set_stage(BuildStage::Preparing, "Preparing: reading your ROM and writing the game's code...");

    // If running from an AppImage (or if the source folder moved), CMakeCache.txt
    // will have the old ephemeral mount path, which causes CMake to abort.
    // If CMAKE_HOME_DIRECTORY in CMakeCache.txt doesn't match j.f.source, remove it.
    namespace fs = std::filesystem;
    std::string cache_file = j.f.game + "/CMakeCache.txt";
    if (file_exists(cache_file)) {
        bool match = false;
        std::ifstream f(fs::u8path(cache_file));
        std::string line;
        while (std::getline(f, line)) {
            if (line.rfind("CMAKE_HOME_DIRECTORY:INTERNAL=", 0) == 0) {
                std::string old_source = line.substr(30);
                while (!old_source.empty() && (old_source.back() == '\r' || old_source.back() == '\n'))
                    old_source.pop_back();
                if (old_source == j.f.source) match = true;
                break;
            }
        }
        if (!match) {
            std::error_code ec;
            fs::remove(fs::u8path(cache_file), ec);
            fs::remove_all(fs::u8path(j.f.game + "/CMakeFiles"), ec);
            fs::remove(fs::u8path(j.f.game + "/build.ninja"), ec);
        }
    }

    std::string configure = quote_arg(j.t.cmake)
        + " -S " + quote_arg(j.f.source) + " -B " + quote_arg(j.f.game)
        + " -G Ninja -DCMAKE_BUILD_TYPE=Release"
        + " " + quote_arg("-DCMAKE_C_COMPILER=" + j.t.clang)
        + " " + quote_arg("-DCMAKE_CXX_COMPILER=" + j.t.clangxx)
        + " " + quote_arg("-DCMAKE_MAKE_PROGRAM=" + j.t.ninja)
        + (!j.t.sdl2_dir.empty() ? " " + quote_arg("-DSDL2_DIR=" + j.t.sdl2_dir) : "")
        + " " + quote_arg("-DPython3_EXECUTABLE=" + j.t.python)
        + " " + quote_arg("-DNESRECOMP_ROOT=" + j.f.nesrecomp)
        + " " + quote_arg("-DNESRECOMP_RECOMP_UI=" + j.f.recomp_ui)
        + " " + quote_arg("-DSHADOWGATE_ROM=" + j.rom);
    int rc = run(configure, j.f.source, error);
    if (rc != 0) {
        finish(rc, *error, why_it_failed("Preparing the build").c_str(), "");
        return rc;
    }
    set_stage(BuildStage::Compiling, "Compiling the game...");
    rc = run(quote_arg(j.t.cmake) + " --build " + quote_arg(j.f.game), j.f.source, error);
    if (rc == 0 && !file_exists(game_exe(j.f))) {
#ifdef _WIN32
        set_stage(BuildStage::Failed, "The build finished but shadowgate.exe isn't there. Show the details below.");
#else
        set_stage(BuildStage::Failed, "The build finished but shadowgate isn't there. Show the details below.");
#endif
        return 1;
    }
    finish(rc, *error, why_it_failed("Compiling").c_str(), "Built! Shadowgate is ready to play.");
    return rc;
}

// ---- downloading -----------------------------------------------------------------
int get_python(const Job &j, std::string *error)
{
    (void)error;
    set_stage(BuildStage::Downloading, "Downloading Python (about 22 MB)...");
    std::string work = j.f.tools + "/.downloads";
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::u8path(work), ec);
    std::string archive = work + "/python.tar.gz";
    add_line(std::string("> download ") + kPythonUrl);

    auto report = [](long long done, long long total) {
        static Uint32 last;
        Uint32 now = SDL_GetTicks();
        if (now - last < 150 && done != total) return;
        last = now;
        char line[64];
        std::snprintf(line, sizeof line, "@bytes %lld %lld", done, total);
        add_line(line);
    };
    std::string sum, problem;
    for (int attempt = 1; attempt <= 3; ++attempt) {
        problem = http_download(kPythonUrl, archive, &sum, report, [] { return cancel_requested(); });
        if (problem.empty() || problem == "cancelled") break;
        add_line("couldn't download Python (try " + std::to_string(attempt) + " of 3): " + problem);
        SDL_Delay(3000);
    }
    if (problem == "cancelled") {
        std::filesystem::remove(std::filesystem::u8path(archive), ec);
        finish(-2, "", "", "");
        return -2;
    }
    if (!problem.empty()) {
        set_stage(BuildStage::Failed, "Couldn't download Python. Check the internet connection and try again.\n("
                                      + problem + ")");
        return 1;
    }
    if (sum != kPythonSha256) {
        add_line("python.tar.gz SHA-256 is " + sum + ", expected " + kPythonSha256);
        set_stage(BuildStage::Failed, "The Python download was damaged or has changed (its fingerprint "
                                      "doesn't match), so it was not used. Please try again.");
        std::filesystem::remove(std::filesystem::u8path(archive), ec);
        return 1;
    }

    set_stage(BuildStage::Unpacking, "Unpacking Python...");
    add_line("> unpack python.tar.gz into " + j.f.tools);
    problem = extract_tar_gz(archive, j.f.tools, [](long long done, long long total) {
        static Uint32 last;
        Uint32 now = SDL_GetTicks();
        if (now - last < 150 && done != total) return;
        last = now;
        char line[64];
        std::snprintf(line, sizeof line, "@unpack %lld %lld", done, total);
        add_line(line);
    });
    std::filesystem::remove(std::filesystem::u8path(archive), ec);
    if (problem.empty() && !file_exists(j.f.tools + "/python/python.exe")) problem = "python.exe isn't there";
    if (!problem.empty()) {
        set_stage(BuildStage::Failed, "Couldn't unpack Python: " + problem);
        return 1;
    }
    return 0;
}

int run_fetch(const Job &j, const std::string &python, const std::string &dest, const std::string &items,
              const char *done_message, std::string *error)
{
    std::string script = exe_folder() + "/fetch.py";
    if (!file_exists(script)) {
        set_stage(BuildStage::Failed, "fetch.py is missing; it belongs beside the launcher.");
        return 1;
    }
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::u8path(dest), ec);
    set_stage(BuildStage::Downloading, "Starting the download...");
    { Locked l; s_error_line.clear(); }
    std::string command = quote_arg(python) + " -I -u " + quote_arg(script) + " " + quote_arg(dest);
    size_t at = 0;
    while (at < items.size()) {             // one argument per item
        size_t end = items.find(' ', at);
        if (end == std::string::npos) end = items.size();
        command += " " + items.substr(at, end - at);
        at = end + 1;
    }
    int rc = run(command, dest, error);
    std::string reason;
    { Locked l; reason = s_error_line; }
    if (reason.empty()) reason = "The download stopped. Show the details below.";
    finish(rc, *error, reason.c_str(), done_message);
    (void)j;
    return rc;
}

int job_thread(void *)
{
    const Job &j = s_job;
    std::string error;
    {
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::u8path(j.f.game), ec);
        s_log_file = utf8_fopen((exe_folder() + "/launcher-build.log").c_str(), "wb");
    }
    int rc = 0;
    if (j.kind == JobKind::Build) {
        // The compiler and Ninja must be findable by the programs CMake starts.
        static bool s_path_set;
        if (!s_path_set) { prepend_to_path(folder_of(j.t.clang) + ";" + folder_of(j.t.ninja)); s_path_set = true; }
        rc = do_build(j, &error);
    } else if (j.kind == JobKind::Tools) {
        std::string python = j.t.python;
        if (j.t.need_python) {
            rc = get_python(j, &error);
            python = j.f.tools + "/python/python.exe";
        }
        if (rc == 0 && !j.t.fetch_items.empty())
            rc = run_fetch(j, python, j.f.tools, j.t.fetch_items, "The build tools are ready.", &error);
        else if (rc == 0)
            set_stage(BuildStage::Finished, "The build tools are ready.");
    } else if (j.kind == JobKind::Voice) {
        std::string python = j.t.python;
        if (python.empty()) {                    // the voice can come before the build tools
            rc = get_python(j, &error);
            python = j.f.tools + "/python/python.exe";
        }
        if (rc == 0)
            rc = run_fetch(j, python, j.v.folder, j.v.fetch_items, "The narrator voice is installed.", &error);
    }
    add_line(rc == 0 ? "-- the launcher: finished" : "-- the launcher: stopped");
    if (s_log_file) { std::fclose(s_log_file); s_log_file = nullptr; }
    Locked l;
    s_running = false;
    return 0;
}

void start(const Job &job)
{
    if (!s_lock) s_lock = SDL_CreateMutex();
    if (build_running()) return;
    if (s_thread) { SDL_WaitThread(s_thread, nullptr); s_thread = nullptr; }
    reset_cancel();
    s_job = job;
    for (char &c : s_job.rom) if (c == '\\') c = '/';
    {
        Locked l;
        s_log.clear();
        s_progress = BuildProgress();
        s_progress.kind = job.kind;
        s_progress.stage = BuildStage::Preparing;
        s_running = true;
    }
    s_thread = SDL_CreateThread(job_thread, "launcher job", nullptr);
    if (!s_thread) {
        Locked l;
        s_running = false;
        s_progress.stage = BuildStage::Failed;
        s_progress.message = std::string("Couldn't start: ") + SDL_GetError();
    }
}

} // namespace

void build_start(const Folders &f, const Tools &t, const std::string &rom_path)
{
    start(Job{ JobKind::Build, f, t, Voice(), rom_path });
}

void fetch_tools_start(const Folders &f, const Tools &t)
{
    start(Job{ JobKind::Tools, f, t, Voice(), std::string() });
}

void fetch_voice_start(const Folders &f, const Tools &t, const Voice &v)
{
    start(Job{ JobKind::Voice, f, t, v, std::string() });
}

bool build_running()
{
    if (!s_lock) return false;
    Locked l;
    return s_running;
}

BuildProgress build_progress()
{
    if (!s_lock) return BuildProgress();
    Locked l;
    return s_progress;
}

void build_cancel() { if (build_running()) cancel_process(); }

void build_shutdown()
{
    build_cancel();
    if (s_thread) { SDL_WaitThread(s_thread, nullptr); s_thread = nullptr; }
}

void build_with_log(const std::function<void(const std::vector<std::string> &)> &fn)
{
    if (!s_lock) { fn(std::vector<std::string>()); return; }
    Locked l;
    fn(s_log);
}
