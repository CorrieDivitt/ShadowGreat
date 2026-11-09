// http_download.cpp - see http_download.h.
#include "http_download.h"
#include "sha256.h"
#include "utf8_file.h"

#include <cstdio>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>

static std::wstring W(const std::string &s)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n > 0 ? n : 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    w.resize(n > 0 ? n - 1 : 0);
    return w;
}

static std::string why(const char *what)
{
    return std::string(what) + " (Windows error " + std::to_string(GetLastError()) + ")";
}

namespace {
struct Handle {                                   // closes itself
    HINTERNET h = nullptr;
    ~Handle() { if (h) WinHttpCloseHandle(h); }
};
}

std::string http_download(const std::string &url, const std::string &path, std::string *out_sha256,
                          const std::function<void(long long, long long)> &progress,
                          const std::function<bool()> &stop)
{
    std::wstring wurl = W(url);
    wchar_t host[256], where[4096];
    URL_COMPONENTS uc;
    ZeroMemory(&uc, sizeof uc);
    uc.dwStructSize = sizeof uc;
    uc.lpszHostName = host; uc.dwHostNameLength = 256;
    uc.lpszUrlPath = where; uc.dwUrlPathLength = 4096;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) return why("bad address");

    // The proxy settings of this PC (Windows 8.1+), else the older default.
    Handle session;
    session.h = WinHttpOpen(L"Shadowgreat-launcher", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session.h)
        session.h = WinHttpOpen(L"Shadowgreat-launcher", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session.h) return why("couldn't start the download");
    WinHttpSetTimeouts(session.h, 0, 60000, 60000, 60000);

    Handle connection, request;
    connection.h = WinHttpConnect(session.h, host, uc.nPort, 0);
    if (!connection.h) return why("couldn't connect");
    request.h = WinHttpOpenRequest(connection.h, L"GET", where, nullptr, WINHTTP_NO_REFERER,
                                   WINHTTP_DEFAULT_ACCEPT_TYPES,
                                   uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
    if (!request.h) return why("couldn't connect");
    if (!WinHttpSendRequest(request.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
        || !WinHttpReceiveResponse(request.h, nullptr))
        return why("couldn't reach the download server");

    DWORD status = 0, size = sizeof status;
    WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
    if (status != 200) return "the server answered " + std::to_string(status);
    long long total = 0;
    wchar_t length[32];
    size = sizeof length;
    if (WinHttpQueryHeaders(request.h, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX,
                            length, &size, WINHTTP_NO_HEADER_INDEX))
        total = _wtoi64(length);

    FILE *out = utf8_fopen(path.c_str(), "wb");
    if (!out) return "couldn't write " + path;
    Sha256 sum;
    sha256_init(&sum);
    long long done = 0;
    static char buf[1 << 16];
    std::string error;
    for (;;) {
        if (stop && stop()) { error = "cancelled"; break; }
        DWORD got = 0;
        if (!WinHttpReadData(request.h, buf, sizeof buf, &got)) { error = why("the download was interrupted"); break; }
        if (got == 0) break;
        if (std::fwrite(buf, 1, got, out) != got) { error = "couldn't write " + path; break; }
        sha256_update(&sum, buf, got);
        done += got;
        if (progress) progress(done, total);
    }
    if (std::fclose(out) != 0 && error.empty()) error = "couldn't write " + path;
    if (error.empty() && total && done != total) error = "the download was cut short";
    char hex[65];
    sha256_hex(&sum, hex);
    if (out_sha256) *out_sha256 = hex;
    return error;
}

#else   // not Windows: only so the code can be checked elsewhere

std::string http_download(const std::string &, const std::string &, std::string *,
                          const std::function<void(long long, long long)> &, const std::function<bool()> &)
{
    return "downloads only work on Windows";
}

#endif
