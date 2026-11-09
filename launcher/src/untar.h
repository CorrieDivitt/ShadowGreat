// untar.h - unpacking a .tar.gz (the Python download) into a folder.
#pragma once
#include <functional>
#include <string>

// Returns "" on success, or what went wrong. progress(done, total) counts bytes.
// Paths that would land outside `into` are refused; links are skipped.
std::string extract_tar_gz(const std::string &archive, const std::string &into,
                           const std::function<void(long long, long long)> &progress);
