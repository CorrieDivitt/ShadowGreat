// build_job.h - building the game in the background (CMake, then Ninja).
#pragma once
#include "paths.h"
#include <functional>
#include <string>
#include <vector>

enum class BuildStage { Idle, Preparing, Compiling, Finished, Failed, Cancelled };

struct BuildProgress {
    BuildStage stage = BuildStage::Idle;
    int done = 0, total = 0;     // files compiled so far, out of total (while Compiling)
    std::string message;         // what to tell the player
};

void build_start(const Folders &f, const Tools &t, const std::string &rom_path);
bool build_running();
BuildProgress build_progress();
void build_cancel();
void build_shutdown();           // cancel and wait (the launcher is closing)

// Look at the output so far. fn runs under a lock: keep it quick.
void build_with_log(const std::function<void(const std::vector<std::string> &)> &fn);
