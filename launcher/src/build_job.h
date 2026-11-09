// build_job.h - the launcher's background work, one job at a time:
// building the game (CMake, then Ninja), and downloading the build tools or
// the optional narrator voice (fetch.py).
#pragma once
#include "paths.h"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

enum class JobKind { None, Build, Tools, Voice };
enum class BuildStage { Idle, Preparing, Compiling, Downloading, Unpacking, Finished, Failed, Cancelled };

struct BuildProgress {
    JobKind kind = JobKind::None;
    BuildStage stage = BuildStage::Idle;
    int64_t done = 0, total = 0; // files compiled, bytes downloaded or files unpacked
    std::string message;         // what to tell the player
};

void build_start(const Folders &f, const Tools &t, const std::string &rom_path);
void fetch_tools_start(const Folders &f, const Tools &t);
void fetch_voice_start(const Folders &f, const Tools &t, const Voice &v);

bool build_running();            // any job
BuildProgress build_progress();  // the current or last job
void build_cancel();
void build_shutdown();           // cancel and wait (the launcher is closing)

// Look at the output so far. fn runs under a lock: keep it quick.
void build_with_log(const std::function<void(const std::vector<std::string> &)> &fn);
