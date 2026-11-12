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

#else   // POSIX / Linux (libcurl)

#include <curl/curl.h>

namespace {
struct DownloadContext {
    FILE *out = nullptr;
    Sha256 *sum = nullptr;
    long long total = 0;
    long long done = 0;
    const std::function<void(long long, long long)> *progress = nullptr;
    const std::function<bool()> *stop = nullptr;
    bool cancelled = false;
};

static size_t write_callback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    DownloadContext *ctx = static_cast<DownloadContext *>(userdata);
    if (ctx->stop && (*ctx->stop)()) {
        ctx->cancelled = true;
        return 0;
    }
    size_t bytes = size * nmemb;
    if (ctx->out && std::fwrite(ptr, 1, bytes, ctx->out) != bytes) return 0;
    if (ctx->sum) sha256_update(ctx->sum, ptr, bytes);
    ctx->done += (long long)bytes;
    if (ctx->progress) (*ctx->progress)(ctx->done, ctx->total);
    return bytes;
}

static int xferinfo_callback(void *clientp, curl_off_t dltotal, curl_off_t dlnow,
                             curl_off_t, curl_off_t)
{
    DownloadContext *ctx = static_cast<DownloadContext *>(clientp);
    if (ctx->stop && (*ctx->stop)()) {
        ctx->cancelled = true;
        return 1;
    }
    if (dltotal > 0) ctx->total = (long long)dltotal;
    if (ctx->progress && dlnow > 0) (*ctx->progress)((long long)dlnow, ctx->total);
    return 0;
}
}

std::string http_download(const std::string &url, const std::string &path, std::string *out_sha256,
                          const std::function<void(long long, long long)> &progress,
                          const std::function<bool()> &stop)
{
    CURL *curl = curl_easy_init();
    if (!curl) return "Failed to initialize curl";

    FILE *out = utf8_fopen(path.c_str(), "wb");
    if (!out) {
        curl_easy_cleanup(curl);
        return "couldn't write " + path;
    }

    Sha256 sum;
    sha256_init(&sum);

    DownloadContext ctx;
    ctx.out = out;
    ctx.sum = &sum;
    ctx.progress = &progress;
    ctx.stop = &stop;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Shadowgreat-launcher");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, xferinfo_callback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);

    CURLcode res = curl_easy_perform(curl);
    std::fclose(out);

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    if (ctx.cancelled) return "cancelled";
    if (res != CURLE_OK) {
        return std::string("Download failed: ") + curl_easy_strerror(res);
    }
    if (http_code != 200) {
        return "the server answered " + std::to_string(http_code);
    }

    char hex[65];
    sha256_hex(&sum, hex);
    if (out_sha256) *out_sha256 = hex;
    return "";
}

#endif
