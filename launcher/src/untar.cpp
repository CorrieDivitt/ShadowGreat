// untar.cpp - see untar.h. Understands the tar formats in use today: ustar,
// pax extended headers and GNU long names.
#include "untar.h"
#include "inflate.h"
#include "utf8_file.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

static long long octal(const char *p, int n)
{
    long long v = 0;
    for (int i = 0; i < n && p[i]; ++i) {
        if (p[i] == ' ') continue;
        if (p[i] < '0' || p[i] > '7') break;
        v = v * 8 + (p[i] - '0');
    }
    return v;
}

static std::string field(const char *p, int n)
{
    int len = 0;
    while (len < n && p[len]) ++len;
    return std::string(p, len);
}

// A path inside the archive is safe if it's relative and never goes up.
static bool safe(const std::string &name)
{
    if (name.empty() || name[0] == '/' || name[0] == '\\' || name.find(':') != std::string::npos) return false;
    size_t at = 0;
    while (at <= name.size()) {
        size_t end = name.find_first_of("/\\", at);
        if (end == std::string::npos) end = name.size();
        if (name.compare(at, end - at, "..") == 0 && end - at == 2) return false;
        at = end + 1;
    }
    return true;
}

static std::string pax_path(const char *data, long long size)
{
    std::string path;
    long long at = 0;
    while (at < size) {                            // records: "<len> key=value\n"
        long long len = std::atoll(data + at);
        if (len <= 0 || at + len > size) break;
        std::string rec(data + at, (size_t)len);
        size_t sp = rec.find(' '), eq = rec.find('=');
        if (sp != std::string::npos && eq != std::string::npos && rec.compare(sp + 1, eq - sp - 1, "path") == 0)
            path = rec.substr(eq + 1, rec.size() - eq - 2);
        at += len;
    }
    return path;
}

std::string extract_tar_gz(const std::string &archive, const std::string &into,
                           const std::function<void(long long, long long)> &progress)
{
    std::vector<uint8_t> packed;
    {
        FILE *f = utf8_fopen(archive.c_str(), "rb");
        if (!f) return "couldn't open " + archive;
        uint8_t buf[1 << 16];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) packed.insert(packed.end(), buf, buf + n);
        std::fclose(f);
    }
    uint8_t *tar = nullptr;
    size_t size = 0;
    if (int err = gunzip(packed.data(), packed.size(), &tar, &size))
        return "the archive is damaged (gzip error " + std::to_string(err) + ")";
    packed.clear();
    packed.shrink_to_fit();

    std::string result, long_name;
    size_t pos = 0;
    while (pos + 512 <= size) {
        const char *h = (const char *)tar + pos;
        if (h[0] == 0) break;                      // the end blocks
        long long len = octal(h + 124, 12);
        char type = h[156];
        size_t data = pos + 512;
        if (len < 0 || data + (size_t)len > size) { result = "the archive is cut short"; break; }
        pos = data + (size_t)((len + 511) / 512 * 512);

        if (type == 'x' || type == 'L') {          // the next entry's long name
            long_name = type == 'x' ? pax_path((const char *)tar + data, len)
                                    : field((const char *)tar + data, (int)len);
            continue;
        }
        if (type == 'g') continue;
        std::string name = long_name;
        long_name.clear();
        if (name.empty()) {
            name = field(h, 100);
            if (std::memcmp(h + 257, "ustar", 5) == 0 && h[345]) name = field(h + 345, 155) + "/" + name;
        }
        if (!safe(name)) { result = "the archive has an unsafe path: " + name; break; }
        fs::path target = fs::u8path(into + "/" + name);
        std::error_code ec;
        if (type == '5') {
            fs::create_directories(target, ec);
        } else if (type == '0' || type == '\0' || type == '7') {
            fs::create_directories(target.parent_path(), ec);
            FILE *out = utf8_fopen(target.u8string().c_str(), "wb");
            if (!out || std::fwrite(tar + data, 1, (size_t)len, out) != (size_t)len) {
                if (out) std::fclose(out);
                result = "couldn't write " + target.u8string();
                break;
            }
            std::fclose(out);
        }                                          // links and devices: skipped
        if (progress) progress((long long)pos, (long long)size);
    }
    std::free(tar);
    return result;
}
