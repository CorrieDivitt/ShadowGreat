// http_download.h - downloading a file with Windows' own WinHTTP (no other
// program is started), checking its SHA-256 as it arrives.
#pragma once
#include <functional>
#include <string>

// Returns "" on success, or what went wrong. out_sha256 gets the fingerprint
// of what was downloaded. Stops (with "cancelled") when stop() says so.
std::string http_download(const std::string &url, const std::string &path, std::string *out_sha256,
                          const std::function<void(long long, long long)> &progress,
                          const std::function<bool()> &stop);
