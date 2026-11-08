// child_process.h - running the build tools and the game.
#pragma once
#include <functional>
#include <string>

// Quote one command-line argument if it needs it.
std::string quote_arg(const std::string &arg);

// Put these folders (UTF-8, ';'-separated) in front of PATH for programs we start.
void prepend_to_path(const std::string &folders);

// Run a program with no window, hand each line of its output (stdout and
// stderr) to on_line, and wait. Returns its exit code, -1 if it could not be
// started (*error says why), or -2 if cancel_process() stopped it.
int run_process(const std::string &command_line, const std::string &work_folder,
                const std::function<void(const std::string &)> &on_line, std::string *error);

// Stop the program run_process() is running, and every program it started.
// Safe from any thread. Stays in effect until reset_cancel().
void cancel_process();
void reset_cancel();

// Start a program and don't wait for it (the game).
bool start_program(const std::string &exe, const std::string &work_folder);
