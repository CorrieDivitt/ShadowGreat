// child_process.cpp - see child_process.h. Windows only (the launcher is a Windows program).
#include "child_process.h"

#include <atomic>
#include <mutex>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

std::string quote_arg(const std::string &arg)
{
    if (!arg.empty() && arg.find_first_of(" \t\"") == std::string::npos) return arg;
    std::string out = "\"";
    for (char c : arg) { if (c == '"') out += '\\'; out += c; }
    return out + "\"";
}

static std::atomic<bool> s_cancelled{ false };
void reset_cancel() { s_cancelled = false; }

#ifdef _WIN32

static std::wstring W(const std::string &s)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n > 0 ? n : 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    w.resize(n > 0 ? n - 1 : 0);
    return w;
}

static std::string last_error_text()
{
    wchar_t *msg = nullptr;
    DWORD code = GetLastError();
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, code, 0, (wchar_t *)&msg, 0, nullptr);
    char utf8[1024] = "";
    if (msg) {
        WideCharToMultiByte(CP_UTF8, 0, msg, -1, utf8, sizeof utf8, nullptr, nullptr);
        LocalFree(msg);
    }
    std::string s(utf8);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    return s + " (" + std::to_string(code) + ")";
}

void prepend_to_path(const std::string &folders)
{
    std::wstring add = W(folders);
    DWORD n = GetEnvironmentVariableW(L"PATH", nullptr, 0);
    std::wstring old(n, L'\0');
    if (n) { GetEnvironmentVariableW(L"PATH", &old[0], n); old.resize(n - 1); }
    std::wstring now = add + L";" + old;
    SetEnvironmentVariableW(L"PATH", now.c_str());
}

static std::mutex s_job_lock;
static HANDLE s_job;

void cancel_process()
{
    s_cancelled = true;
    std::lock_guard<std::mutex> g(s_job_lock);
    if (s_job) TerminateJobObject(s_job, 1);
}

int run_process(const std::string &command_line, const std::string &work_folder,
                const std::function<void(const std::string &)> &on_line, std::string *error)
{
    if (s_cancelled) return -2;
    SECURITY_ATTRIBUTES sa = { sizeof sa, nullptr, TRUE };
    HANDLE rd, wr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) { if (error) *error = last_error_text(); return -1; }
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                             OPEN_EXISTING, 0, nullptr);

    // A job holds the program and everything it starts (ninja -> clang), so
    // Cancel, or closing the launcher, stops all of them.
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION li;
    ZeroMemory(&li, sizeof li);
    li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(job, JobObjectExtendedLimitInformation, &li, sizeof li);

    STARTUPINFOW si;
    ZeroMemory(&si, sizeof si);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = nul;
    si.hStdOutput = wr;
    si.hStdError = wr;
    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof pi);
    std::wstring cmd = W(command_line), wd = W(work_folder);
    BOOL ok = CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED,
                             nullptr, wd.empty() ? nullptr : wd.c_str(), &si, &pi);
    if (!ok && error) *error = last_error_text();
    CloseHandle(wr);
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    if (!ok) { CloseHandle(rd); CloseHandle(job); return -1; }

    AssignProcessToJobObject(job, pi.hProcess);
    {
        std::lock_guard<std::mutex> g(s_job_lock);
        s_job = job;
    }
    if (s_cancelled) TerminateJobObject(job, 1);
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);

    std::string pending;
    char buf[4096];
    DWORD got;
    while (ReadFile(rd, buf, sizeof buf, &got, nullptr) && got > 0) {
        pending.append(buf, got);
        size_t at;
        while ((at = pending.find('\n')) != std::string::npos) {
            std::string line = pending.substr(0, at);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            on_line(line);
            pending.erase(0, at + 1);
        }
    }
    if (!pending.empty()) on_line(pending);

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    {
        std::lock_guard<std::mutex> g(s_job_lock);
        s_job = nullptr;
    }
    CloseHandle(job);
    CloseHandle(pi.hProcess);
    CloseHandle(rd);
    return s_cancelled ? -2 : (int)code;
}

static HANDLE s_game;

bool game_running()
{
    if (!s_game) return false;
    if (WaitForSingleObject(s_game, 0) == WAIT_TIMEOUT) return true;
    CloseHandle(s_game);
    s_game = nullptr;
    return false;
}

bool start_program(const std::string &exe, const std::string &work_folder)
{
    STARTUPINFOW si;
    ZeroMemory(&si, sizeof si);
    si.cb = sizeof si;
    PROCESS_INFORMATION pi;
    std::wstring cmd = W(quote_arg(exe)), wd = W(work_folder);
    if (!CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, 0, nullptr, wd.c_str(), &si, &pi))
        return false;
    CloseHandle(pi.hThread);
    if (s_game) CloseHandle(s_game);
    s_game = pi.hProcess;
    return true;
}

#else   // not Windows: only so the code can be checked elsewhere

void prepend_to_path(const std::string &) {}
void cancel_process() { s_cancelled = true; }
int run_process(const std::string &, const std::string &, const std::function<void(const std::string &)> &,
                std::string *error)
{
    if (error) *error = "the launcher only runs programs on Windows";
    return -1;
}
bool start_program(const std::string &, const std::string &) { return false; }
bool game_running() { return false; }

#endif
