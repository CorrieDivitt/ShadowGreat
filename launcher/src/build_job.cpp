// build_job.cpp - see build_job.h.
//
// The same two steps as building by hand in the tools window:
//   cmake -S <source> -B <game> -G Ninja ... -DSHADOWGATE_ROM=<your ROM>
//   cmake --build <game>
// Everything they write (including the code made from the ROM) goes in the
// game folder on this PC. The output is also saved to launcher-build.log.
#include "build_job.h"
#include "child_process.h"
#include "utf8_file.h"

#include <SDL.h>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <system_error>

namespace {

SDL_mutex *s_lock;
SDL_Thread *s_thread;
bool s_running;
BuildProgress s_progress;
std::vector<std::string> s_log;
FILE *s_log_file;

struct Job { Folders f; Tools t; std::string rom; };
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
}

// One line of output: keep it, save it, and read Ninja's "[12/345]" counter.
void add_line(const std::string &line)
{
    if (s_log_file) { std::fputs(line.c_str(), s_log_file); std::fputc('\n', s_log_file); std::fflush(s_log_file); }
    int done, total;
    bool counter = line.size() > 4 && line[0] == '[' && std::sscanf(line.c_str(), "[%d/%d]", &done, &total) == 2;
    Locked l;
    if (s_log.size() >= kMaxLines) s_log.erase(s_log.begin(), s_log.begin() + kMaxLines / 10);
    s_log.push_back(line);
    if (counter && total > 0) { s_progress.done = done; s_progress.total = total; }
}

bool contains(const std::string &s, const char *what) { return s.find(what) != std::string::npos; }

// A short reason for the player, picked out of the output.
std::string why_it_failed(const char *step)
{
    Locked l;
    std::string reason;
    for (size_t i = 0; i < s_log.size(); ++i) {
        const std::string &line = s_log[i];
        if (contains(line, "Could NOT find Python3") || contains(line, "Python3"))
            if (contains(line, "NOT find") || contains(line, "not find")) {
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

int build_thread(void *)
{
    const Job &j = s_job;
    std::string error;
    {
        std::string log = j.f.game + "/../launcher-build.log";
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::u8path(j.f.game), ec);
        s_log_file = utf8_fopen(log.c_str(), "wb");
    }

    // The compiler and Ninja must be findable by the programs CMake starts.
    static bool s_path_set;
    if (!s_path_set) { prepend_to_path(folder_of(j.t.clang) + ";" + folder_of(j.t.ninja)); s_path_set = true; }

    set_stage(BuildStage::Preparing, "Preparing: reading your ROM and writing the game's code...");
    std::string configure = quote_arg(j.t.cmake)
        + " -S " + quote_arg(j.f.source) + " -B " + quote_arg(j.f.game)
        + " -G Ninja -DCMAKE_BUILD_TYPE=Release"
        + " " + quote_arg("-DCMAKE_C_COMPILER=" + j.t.clang)
        + " " + quote_arg("-DCMAKE_CXX_COMPILER=" + j.t.clangxx)
        + " " + quote_arg("-DCMAKE_MAKE_PROGRAM=" + j.t.ninja)
        + " " + quote_arg("-DSDL2_DIR=" + j.t.sdl2_dir)
        + " " + quote_arg("-DNESRECOMP_ROOT=" + j.f.nesrecomp)
        + " " + quote_arg("-DNESRECOMP_RECOMP_UI=" + j.f.recomp_ui)
        + " " + quote_arg("-DSHADOWGATE_ROM=" + j.rom);
    int rc = run(configure, j.f.source, &error);

    if (rc == 0) {
        set_stage(BuildStage::Compiling, "Compiling the game...");
        rc = run(quote_arg(j.t.cmake) + " --build " + quote_arg(j.f.game), j.f.source, &error);
        if (rc == -2) set_stage(BuildStage::Cancelled, "Build cancelled. Press Build to start again.");
        else if (rc == -1) set_stage(BuildStage::Failed, "Couldn't start CMake: " + error);
        else if (rc != 0) set_stage(BuildStage::Failed, why_it_failed("Compiling"));
        else if (!file_exists(game_exe(j.f)))
            set_stage(BuildStage::Failed, "The build finished but shadowgate.exe isn't there. Show the details below.");
        else
            set_stage(BuildStage::Finished, "Built! Shadowgate is ready to play.");
    } else if (rc == -2) {
        set_stage(BuildStage::Cancelled, "Build cancelled. Press Build to start again.");
    } else if (rc == -1) {
        set_stage(BuildStage::Failed, "Couldn't start CMake: " + error);
    } else {
        set_stage(BuildStage::Failed, why_it_failed("Preparing the build"));
    }

    add_line(rc == 0 ? "-- the launcher: build finished" : "-- the launcher: build stopped");
    if (s_log_file) { std::fclose(s_log_file); s_log_file = nullptr; }
    Locked l;
    s_running = false;
    return 0;
}

} // namespace

void build_start(const Folders &f, const Tools &t, const std::string &rom_path)
{
    if (!s_lock) s_lock = SDL_CreateMutex();
    if (build_running()) return;
    if (s_thread) { SDL_WaitThread(s_thread, nullptr); s_thread = nullptr; }
    reset_cancel();
    s_job = Job{ f, t, rom_path };
    for (char &c : s_job.rom) if (c == '\\') c = '/';
    {
        Locked l;
        s_log.clear();
        s_progress = BuildProgress();
        s_progress.stage = BuildStage::Preparing;
        s_running = true;
    }
    s_thread = SDL_CreateThread(build_thread, "build", nullptr);
    if (!s_thread) {
        Locked l;
        s_running = false;
        s_progress.stage = BuildStage::Failed;
        s_progress.message = std::string("Couldn't start the build: ") + SDL_GetError();
    }
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
