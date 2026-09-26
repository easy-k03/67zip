#include "port.hpp"

#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#if !defined(ZIP67_NO_FILESYSTEM)
#  include <filesystem>
#endif

#if defined(ZIP67_OS_WINDOWS)
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  if defined(ZIP67_WIN32_STUB)
#    include "win32_stub.h"
#  else
#    include <windows.h>
#  endif
#elif defined(ZIP67_OS_POSIX_FAMILY)
#  include <cerrno>
#  include <dirent.h>
#  include <sys/stat.h>
#  include <sys/types.h>
#  include <unistd.h>
#  include <utime.h>
#  if !defined(ZIP67_OS_ANDROID) || __ANDROID_API__ >= 21
#    include <termios.h>
#    define ZIP67_HAVE_TERMIOS 1
#  endif
#elif defined(ZIP67_OS_FREEDOS)
#  include <cerrno>
#  include <dirent.h>
#  include <sys/stat.h>
#  include <unistd.h>
#else
// KolibriOS, Redox, Serenity: C file API only. No termios, no dirent.
#  include <cerrno>
#endif

namespace zip67 {
namespace port {
namespace {

bool match_here(const char* p, const char* t);

bool match_star(const char* p, const char* t) {
    // '*' does not cross '/'. This matches fnmatch(FNM_PATHNAME) and the
    // 7-Zip wildcard rules closely enough for -i/-x filters.
    while (*t && *t != '/') {
        if (match_here(p, t)) return true;
        ++t;
    }
    return match_here(p, t);
}

bool match_class(const char*& p, char c) {
    if (*p == '\0') return false;
    bool neg = false;
    if (*p == '!' || *p == '^') {
        neg = true;
        ++p;
    }
    bool hit = false;
    char prev = 0;
    bool first = true;
    while (*p && *p != ']') {
        if (*p == '-' && !first && p[1] && p[1] != ']') {
            char end = p[1];
            if (c >= prev && c <= end) hit = true;
            p += 2;
            prev = end;
        } else {
            prev = *p;
            if (c == *p) hit = true;
            ++p;
        }
        first = false;
    }
    if (*p == ']') ++p;
    return neg ? !hit : hit;
}

bool match_here(const char* p, const char* t) {
    for (;;) {
        if (*p == '\0') return *t == '\0';
        if (*p == '*') {
            ++p;
            return match_star(p, t);
        }
        if (*t == '\0') return false;
        if (*p == '?') {
            if (*t == '/') return false;
            ++p;
            ++t;
            continue;
        }
        if (*p == '[') {
            ++p;
            if (*t == '/' || !match_class(p, *t)) return false;
            ++t;
            continue;
        }
        if (*p == '\\' && p[1]) ++p;
        if (*p != *t) return false;
        ++p;
        ++t;
    }
}

}  // namespace

const char* os_name() {
#if defined(ZIP67_OS_WINDOWS)
    return "windows";
#elif defined(ZIP67_OS_IOS)
    return "ios";
#elif defined(ZIP67_OS_MACOS)
    return "macos";
#elif defined(ZIP67_OS_ANDROID)
    return "android";
#elif defined(ZIP67_OS_HAIKU)
    return "haiku";
#elif defined(ZIP67_OS_ILLUMOS)
    return "illumos";
#elif defined(ZIP67_OS_SOLARIS)
    return "solaris";
#elif defined(ZIP67_OS_DRAGONFLY)
    return "dragonfly";
#elif defined(ZIP67_OS_GHOSTBSD)
    return "ghostbsd";
#elif defined(__FreeBSD__)
    return "freebsd";
#elif defined(__NetBSD__)
    return "netbsd";
#elif defined(__OpenBSD__)
    return "openbsd";
#elif defined(ZIP67_OS_REDOX)
    return "redox";
#elif defined(ZIP67_OS_SERENITY)
    return "serenity";
#elif defined(ZIP67_OS_KOLIBRI)
    return "kolibrios";
#elif defined(ZIP67_OS_FREEDOS)
    return "freedos";
#elif defined(ZIP67_OS_LINUX)
#  if defined(__GLIBC__)
    return "linux-glibc";
#  elif defined(__MUSL__) || defined(ZIP67_MUSL)
    return "linux-musl";
#  else
    // Alpine and some musl toolchains do not define __MUSL__.
    return "linux";
#  endif
#else
    return "posix";
#endif
}

bool os_ready() { return true; }

const char* arch_name() {
#if defined(__x86_64__) || defined(_M_X64) || defined(__amd64__)
    return "x64";
#elif defined(__i386__) || defined(_M_IX86)
    return "x86";
#elif defined(__aarch64__) || defined(_M_ARM64)
    return "arm64";
#elif defined(__arm__) || defined(_M_ARM)
    return "arm";
#elif defined(__riscv) && (__riscv_xlen == 64)
    return "riscv64";
#elif defined(__sparc__) || defined(__sparc)
    return "sparc";
#elif defined(__powerpc64__) || defined(__ppc64__)
    return "ppc64";
#else
    return "unknown";
#endif
}

char native_sep() {
#if defined(ZIP67_OS_WINDOWS)
    return '\\';
#else
    return '/';
#endif
}

bool wild_match(const char* pattern, const char* text) {
    if (!pattern || !text) return false;
    return match_here(pattern, text);
}

uint32_t file_mode(const std::string& path, uint32_t fallback) {
    FileInfo info;
    if (!stat_path(path, info) || !info.exists) return fallback & modebit::perm;
    return info.mode ? info.mode : (fallback & modebit::perm);
}

bool set_file_mode(const std::string& path, uint32_t mode) {
#if defined(ZIP67_OS_POSIX_FAMILY) || defined(ZIP67_OS_FREEDOS)
    return chmod(path.c_str(), static_cast<mode_t>(mode & modebit::perm)) == 0;
#else
    (void)path;
    (void)mode;
    return true;
#endif
}

bool set_mtime(const std::string& path, int64_t unix_seconds) {
    if (unix_seconds <= 0) return false;
#if defined(ZIP67_OS_WINDOWS)
    HANDLE h = CreateFileA(path.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    // Unix epoch -> FILETIME (100 ns ticks since 1601-01-01).
    int64_t ticks = (unix_seconds + 11644473600LL) * 10000000LL;
    FILETIME ft;
    ft.dwLowDateTime = static_cast<DWORD>(ticks & 0xffffffffu);
    ft.dwHighDateTime = static_cast<DWORD>(static_cast<uint64_t>(ticks) >> 32);
    BOOL ok = SetFileTime(h, nullptr, &ft, &ft);
    CloseHandle(h);
    return ok != 0;
#elif defined(ZIP67_OS_POSIX_FAMILY)
    struct utimbuf tb {};
    tb.actime = static_cast<time_t>(unix_seconds);
    tb.modtime = static_cast<time_t>(unix_seconds);
    return utime(path.c_str(), &tb) == 0;
#else
    // FreeDOS, KolibriOS, Redox, Serenity: no portable utime. The archive
    // still stores the timestamp; the extracted file keeps the host's mtime.
    (void)path;
    return false;
#endif
}

std::string read_password(const char* prompt) {
    if (prompt && prompt[0]) std::cerr << prompt;
    std::string s;
#if defined(ZIP67_OS_WINDOWS)
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;
    BOOL have = in && GetConsoleMode(in, &mode);
    if (have) SetConsoleMode(in, mode & ~ENABLE_ECHO_INPUT);
    std::getline(std::cin, s);
    if (have) {
        SetConsoleMode(in, mode);
        std::cerr << "\n";
    }
#elif defined(ZIP67_HAVE_TERMIOS)
    termios oldt {};
    bool have = tcgetattr(STDIN_FILENO, &oldt) == 0;
    if (have) {
        termios t = oldt;
        t.c_lflag = static_cast<tcflag_t>(t.c_lflag & ~ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &t);
    }
    std::getline(std::cin, s);
    if (have) {
        tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
        std::cerr << "\n";
    }
#else
    // Serenity, Redox, KolibriOS, FreeDOS: no termios. The password is
    // visible. -p<password> avoids this path.
    std::getline(std::cin, s);
#endif
    return s;
}

std::string format_local_time(int64_t unix_seconds) {
    if (unix_seconds <= 0) return "                   ";
    std::time_t tt = static_cast<std::time_t>(unix_seconds);
    std::tm tm {};
#if defined(__MINGW32__) && !defined(_UCRT)
    // Debian's g++-mingw-w64 (msvcrt) declares localtime_r, not localtime_s.
    if (!localtime_r(&tt, &tm)) return "                   ";
#elif defined(_WIN32)
    // MSVC and MinGW-w64 ucrt (MSYS2 on windows-latest) declare localtime_s.
    if (localtime_s(&tm, &tt) != 0) return "                   ";
#else
    if (!localtime_r(&tt, &tm)) return "                   ";
#endif
    char buf[32];
    if (std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm) == 0) return "                   ";
    return buf;
}

namespace {

std::string join_host(const std::string& dir, const std::string& name) {
    if (dir.empty() || dir == ".") return name;
    char sep = native_sep();
    if (dir.back() == '/' || dir.back() == '\\') return dir + name;
    return dir + sep + name;
}

std::string parent_of(const std::string& path) {
    auto slash = path.find_last_of("/\\");
    if (slash == std::string::npos) return {};
    if (slash == 0) return path.substr(0, 1);
    // Keep "C:" so "C:/file" yields "C:".
    return path.substr(0, slash);
}

}  // namespace

bool stat_path(const std::string& path, FileInfo& out) {
    out = FileInfo{};
#if defined(ZIP67_OS_WINDOWS)
    WIN32_FILE_ATTRIBUTE_DATA ad;
    if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &ad)) return true;
    out.exists = true;
    out.is_dir = (ad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    out.is_file = !out.is_dir;
    out.mode = out.is_dir ? modebit::dir : modebit::file;
    ULARGE_INTEGER sz;
    sz.LowPart = ad.nFileSizeLow;
    sz.HighPart = ad.nFileSizeHigh;
    out.size = sz.QuadPart;
    FILETIME ft = ad.ftLastWriteTime;
    ULARGE_INTEGER t;
    t.LowPart = ft.dwLowDateTime;
    t.HighPart = ft.dwHighDateTime;
    if (t.QuadPart > 116444736000000000ULL)
        out.mtime = static_cast<int64_t>((t.QuadPart - 116444736000000000ULL) / 10000000ULL);
    return true;
#elif defined(ZIP67_OS_POSIX_FAMILY)
    struct stat sb {};
    if (lstat(path.c_str(), &sb) != 0) return true;
    out.exists = true;
    out.is_symlink = S_ISLNK(sb.st_mode);
    out.is_dir = S_ISDIR(sb.st_mode);
    out.is_file = S_ISREG(sb.st_mode);
    out.mode = static_cast<uint32_t>(sb.st_mode) & modebit::perm;
    out.mtime = static_cast<int64_t>(sb.st_mtime);
    out.size = out.is_file ? static_cast<uint64_t>(sb.st_size) : 0;
    if (out.is_symlink) {
        std::vector<char> buf(static_cast<size_t>(sb.st_size) + 1);
        ssize_t n = readlink(path.c_str(), buf.data(), buf.size());
        if (n > 0) out.link.assign(buf.data(), static_cast<size_t>(n));
        for (char& c : out.link)
            if (c == '\\') c = '/';
    }
    return true;
#else
    // C file API. No symlink. A directory is detected by opendir-style failure
    // to open as a file plus a successful directory listing where available.
    FILE* f = std::fopen(path.c_str(), "rb");
    if (f) {
        out.exists = true;
        out.is_file = true;
        out.mode = modebit::file;
        if (std::fseek(f, 0, SEEK_END) == 0) {
            long n = std::ftell(f);
            if (n > 0) out.size = static_cast<uint64_t>(n);
        }
        std::fclose(f);
        return true;
    }
#  if !defined(ZIP67_OS_KOLIBRI)
    // DJGPP and most C libraries can tell a directory from a failed fopen
    // only by trying to read it. list_dir reports that.
    bool any = false;
    bool opened = list_dir(path, [&](const std::string&, bool) { any = true; });
    if (opened) {
        out.exists = true;
        out.is_dir = true;
        out.mode = modebit::dir;
        (void)any;
    }
#  endif
    return true;
#endif
}

bool read_file(const std::string& path, std::string& data, std::string& err) {
    data.clear();
#if defined(ZIP67_OS_WINDOWS)
    HANDLE h = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        err = "Cannot open " + path;
        return false;
    }
    LARGE_INTEGER sz;
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart < 0) {
        CloseHandle(h);
        err = "Cannot stat " + path;
        return false;
    }
    data.resize(static_cast<size_t>(sz.QuadPart));
    size_t off = 0;
    while (off < data.size()) {
        DWORD chunk = 0;
        DWORD want = static_cast<DWORD>(std::min<size_t>(data.size() - off, 1 << 20));
        if (!ReadFile(h, &data[off], want, &chunk, nullptr)) {
            CloseHandle(h);
            err = "Read error: " + path;
            return false;
        }
        if (chunk == 0) break;
        off += chunk;
    }
    CloseHandle(h);
    data.resize(off);
    return true;
#else
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        err = "Cannot open " + path;
        return false;
    }
    if (std::fseek(f, 0, SEEK_END) != 0) {
        std::fclose(f);
        err = "Cannot stat " + path;
        return false;
    }
    long n = std::ftell(f);
    if (n < 0) {
        std::fclose(f);
        err = "Cannot stat " + path;
        return false;
    }
    std::rewind(f);
    data.resize(static_cast<size_t>(n));
    if (n > 0 && std::fread(&data[0], 1, static_cast<size_t>(n), f) != static_cast<size_t>(n)) {
        std::fclose(f);
        err = "Read error: " + path;
        return false;
    }
    std::fclose(f);
    return true;
#endif
}

bool make_dirs(const std::string& path) {
    if (path.empty() || path == "." || path == "/" || path == "\\") return true;
    std::string parent = parent_of(path);
    if (!parent.empty() && parent != path) {
        if (!make_dirs(parent)) return false;
    }
#if defined(ZIP67_OS_WINDOWS)
    if (CreateDirectoryA(path.c_str(), nullptr)) return true;
    return GetLastError() == ERROR_ALREADY_EXISTS;
#elif defined(ZIP67_OS_POSIX_FAMILY)
    if (mkdir(path.c_str(), 0755) == 0) return true;
    return errno == EEXIST;
#else
    if (std::fopen(path.c_str(), "rb")) return true;
    // mkdir is a POSIX call. Constrained hosts that have it (DJGPP) get it
    // from <sys/stat.h>, which is not included here on purpose: a missing
    // mkdir must not fail the compile. Creating the leaf with fopen is not
    // a directory, so report failure and let the caller write the file.
    (void)parent;
    return false;
#endif
}

bool write_file(const std::string& path, const char* data, size_t n, uint32_t mode, std::string& err) {
    std::string parent = parent_of(path);
    if (!parent.empty()) make_dirs(parent);
#if defined(ZIP67_OS_WINDOWS)
    HANDLE h = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                           nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        err = "Cannot create " + path;
        return false;
    }
    size_t off = 0;
    while (off < n) {
        DWORD wrote = 0;
        DWORD want = static_cast<DWORD>(std::min<size_t>(n - off, 1 << 20));
        if (!WriteFile(h, data + off, want, &wrote, nullptr) || wrote == 0) {
            CloseHandle(h);
            err = "Write failed " + path;
            return false;
        }
        off += wrote;
    }
    CloseHandle(h);
    (void)mode;
    return true;
#else
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        err = "Cannot create " + path;
        return false;
    }
    if (n && std::fwrite(data, 1, n, f) != n) {
        std::fclose(f);
        err = "Write failed " + path;
        return false;
    }
    if (std::fclose(f) != 0) {
        err = "Write failed " + path;
        return false;
    }
    set_file_mode(path, mode);
    return true;
#endif
}

bool make_link(const std::string& path, const std::string& target, std::string& err) {
#if defined(ZIP67_NO_SYMLINK)
    // No symlink on this host. Store the target as the file contents so
    // extract still produces the member instead of failing the archive.
    return write_file(path, target.data(), target.size(), modebit::file, err);
#elif defined(ZIP67_OS_POSIX_FAMILY)
    std::string parent = parent_of(path);
    if (!parent.empty()) make_dirs(parent);
    if (symlink(target.c_str(), path.c_str()) != 0) {
        err = "cannot create symlink " + path;
        return false;
    }
    return true;
#else
    return write_file(path, target.data(), target.size(), modebit::file, err);
#endif
}

bool remove_file(const std::string& path) {
#if defined(ZIP67_OS_WINDOWS)
    if (DeleteFileA(path.c_str())) return true;
    return RemoveDirectoryA(path.c_str()) != 0;
#else
    return std::remove(path.c_str()) == 0;
#endif
}

bool list_dir(const std::string& dir, const std::function<void(const std::string& name, bool is_dir)>& fn) {
#if defined(ZIP67_OS_WINDOWS)
    std::string pat = dir;
    if (pat.empty()) pat = ".";
    if (pat.back() != '\\' && pat.back() != '/') pat.push_back('\\');
    pat += "*";
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return false;
    do {
        std::string name = fd.cFileName;
        if (name == "." || name == "..") continue;
        fn(name, (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return true;
#elif defined(ZIP67_OS_POSIX_FAMILY)
    DIR* d = opendir(dir.c_str());
    if (!d) return false;
    while (dirent* ent = readdir(d)) {
        std::string name = ent->d_name;
        if (name == "." || name == "..") continue;
        std::string full = join_host(dir, name);
        struct stat sb {};
        bool is_dir = lstat(full.c_str(), &sb) == 0 && S_ISDIR(sb.st_mode) && !S_ISLNK(sb.st_mode);
        fn(name, is_dir);
    }
    closedir(d);
    return true;
#elif defined(ZIP67_OS_FREEDOS)
    // DJGPP provides opendir. Included only for that port so other
    // constrained hosts do not need dirent.h.
    DIR* d = opendir(dir.c_str());
    if (!d) return false;
    while (dirent* ent = readdir(d)) {
        std::string name = ent->d_name;
        if (name == "." || name == "..") continue;
        fn(name, false);
    }
    closedir(d);
    return true;
#else
    (void)dir;
    (void)fn;
    return false;
#endif
}

std::string current_dir() {
#if defined(ZIP67_OS_WINDOWS)
    char buf[MAX_PATH];
    DWORD n = GetCurrentDirectoryA(MAX_PATH, buf);
    if (n == 0 || n >= MAX_PATH) return ".";
    return buf;
#elif defined(ZIP67_OS_POSIX_FAMILY) || defined(ZIP67_OS_FREEDOS)
    char buf[4096];
    if (!getcwd(buf, sizeof(buf))) return ".";
    return buf;
#else
    return ".";
#endif
}

std::string absolute_path(const std::string& path) {
    if (path.empty()) return current_dir();
#if defined(ZIP67_OS_WINDOWS)
    if (path.size() >= 2 && path[1] == ':') return path;
    if (path[0] == '\\' || path[0] == '/') return path;
#else
    if (path[0] == '/') return path;
#endif
    return join_host(current_dir(), path);
}

}  // namespace port
}  // namespace zip67
