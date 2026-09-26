#pragma once

// Platform seam for 67zip.
//
// The archive format and the command parser stay free of OS calls. Anything
// that differs between hosts goes through this header so a later port only
// has to fill in one translation unit.
//
// Ports:
//   POSIX family (builds with a stock C++17 library + POSIX libc):
//     glibc/musl Linux including NixOS, Android, macOS, iOS,
//     FreeBSD, NetBSD, OpenBSD, DragonFly, GhostBSD, Solaris, illumos,
//     OmniOS, Haiku.
//   Win32 (Windows and ReactOS, same branch): src/port.cpp ZIP67_OS_WINDOWS.
//   Constrained hosts have no POSIX libc. They still compile the same
//   sources because file access goes through port::* and std::filesystem is
//   not used on them:
//     FreeDOS   — DJGPP. No threads, no symlinks. -DZIP67_OS_FREEDOS.
//     KolibriOS — no hosted C++ filesystem. -DZIP67_OS_KOLIBRI.
//     Redox     — relibc is incomplete. -DZIP67_OS_REDOX uses the C file API
//                 instead of POSIX calls relibc may not implement.
//     Serenity  — symlink and termios may be missing. -DZIP67_OS_SERENITY
//                 uses the C file API and skips terminal echo control.
//
// Rules:
//   - Archive paths are always '/'. Never store a host separator.
//   - Permission bits are Unix mode bits (modebit::*), never S_I* / Win32
//     attributes.
//   - No thread is required for a correct archive. ZIP67_NO_THREADS keeps
//     compress, extract, list and test on one thread.
//   - Do not add a new OS call in main.cpp. Add it to port::* first.

#include <cstdint>
#include <ctime>
#include <functional>
#include <string>

#if defined(_WIN32) || defined(_WIN64)
#  define ZIP67_OS_WINDOWS 1
#elif defined(__APPLE__)
#  include <TargetConditionals.h>
#  define ZIP67_OS_APPLE 1
#  if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
#    define ZIP67_OS_IOS 1
#  else
#    define ZIP67_OS_MACOS 1
#  endif
#elif defined(__ANDROID__)
#  define ZIP67_OS_ANDROID 1
#elif defined(__HAIKU__)
#  define ZIP67_OS_HAIKU 1
#elif defined(__illumos__) || defined(__illumos)
#  define ZIP67_OS_ILLUMOS 1
#elif defined(__sun) || defined(__sun__)
#  define ZIP67_OS_SOLARIS 1
#elif defined(__DragonFly__)
#  define ZIP67_OS_DRAGONFLY 1
#elif defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
#  define ZIP67_OS_BSD 1
// GhostBSD does not define its own compiler macro. Pass
// -DZIP67_OS_GHOSTBSD=1 (or rely on the FreeBSD branch, which is the same
// userland) when a package build wants the GhostBSD name in `67zip i`.
#  if defined(ZIP67_OS_GHOSTBSD)
#    define ZIP67_OS_GHOSTBSD 1
#  endif
#elif defined(__redox__)
#  define ZIP67_OS_REDOX 1
#elif defined(__serenity__)
#  define ZIP67_OS_SERENITY 1
#elif defined(__KOLIBRIOS__) || defined(__kolibri__)
#  define ZIP67_OS_KOLIBRI 1
#elif defined(__linux__)
#  define ZIP67_OS_LINUX 1
#elif defined(__DJGPP__) || defined(__MSDOS__) || defined(__DOS__)
#  define ZIP67_OS_FREEDOS 1
#else
#  define ZIP67_OS_POSIX 1
#endif

// Stock POSIX libc. Constrained hosts and Windows are not in this set;
// they use the C file API (or Win32) inside port.cpp.
#if !defined(ZIP67_OS_WINDOWS) && !defined(ZIP67_OS_FREEDOS) && !defined(ZIP67_OS_KOLIBRI) && \
    !defined(ZIP67_OS_REDOX) && !defined(ZIP67_OS_SERENITY)
#  define ZIP67_OS_POSIX_FAMILY 1
#endif

// Hosts where std::filesystem is missing or too incomplete to walk a tree.
// main.cpp uses port::walk / port::open_file on these instead.
#if defined(ZIP67_OS_FREEDOS) || defined(ZIP67_OS_KOLIBRI) || defined(ZIP67_OS_REDOX) || \
    defined(ZIP67_OS_SERENITY)
#  define ZIP67_NO_FILESYSTEM 1
#endif

// No hosted thread runtime. Do not start std::thread; one thread is enough
// for a correct archive.
#if defined(ZIP67_OS_FREEDOS) || defined(ZIP67_OS_KOLIBRI)
#  define ZIP67_NO_THREADS 1
#endif

// Symlinks are a POSIX feature these hosts do not have. Archive members of
// type symlink are extracted as a one-line text file containing the target.
#if defined(ZIP67_OS_WINDOWS) || defined(ZIP67_OS_FREEDOS) || defined(ZIP67_OS_KOLIBRI) || \
    defined(ZIP67_OS_REDOX) || defined(ZIP67_OS_SERENITY)
#  define ZIP67_NO_SYMLINK 1
#endif

// Permission bits stored in the 67z container. These match the classic Unix
// mode bits so an archive written on one host extracts with the same mode on
// another. They are NOT the host's S_I* macros (those differ, and Windows
// does not have them).
namespace zip67 {
namespace modebit {
constexpr uint32_t irusr = 0400;
constexpr uint32_t iwusr = 0200;
constexpr uint32_t ixusr = 0100;
constexpr uint32_t irgrp = 0040;
constexpr uint32_t iwgrp = 0020;
constexpr uint32_t ixgrp = 0010;
constexpr uint32_t iroth = 0004;
constexpr uint32_t iwoth = 0002;
constexpr uint32_t ixoth = 0001;
constexpr uint32_t perm  = 0777;
constexpr uint32_t file  = 0644;
constexpr uint32_t dir   = 0755;
constexpr uint32_t link  = 0777;
}  // namespace modebit

namespace port {

// Stable id, also printed by `67zip i`. One of:
//   linux-glibc, linux-musl, linux, android, macos, ios,
//   freebsd, ghostbsd, netbsd, openbsd, dragonfly,
//   solaris, illumos, haiku, windows, redox, serenity,
//   freedos, kolibrios, posix.
const char* os_name();

// False only when this binary was built without the pieces a full port
// needs (no filesystem walk). A Windows, ReactOS, Redox or Serenity build
// that compiled is ready. FreeDOS and KolibriOS are ready for archive
// commands that do not need threads; os_ready stays true once they compile.
bool os_ready();

struct FileInfo {
    bool exists = false;
    bool is_dir = false;
    bool is_symlink = false;
    bool is_file = false;
    uint32_t mode = 0;
    int64_t mtime = 0;
    uint64_t size = 0;
    std::string link;  // symlink target, '/' separated, empty if none
};

// lstat-style query. Does not follow a final symlink.
bool stat_path(const std::string& path, FileInfo& out);

// Read a whole file. False on error, message in err.
bool read_file(const std::string& path, std::string& data, std::string& err);

// Create parent directories of path. True if the directory exists afterwards.
bool make_dirs(const std::string& path);

// Write a new file, replacing an existing one. Mode is Unix mode bits.
bool write_file(const std::string& path, const char* data, size_t n, uint32_t mode, std::string& err);

// Create a symlink, or on ZIP67_NO_SYMLINK hosts a text file whose contents
// are the target. Returns false only when nothing could be written.
bool make_link(const std::string& path, const std::string& target, std::string& err);

bool remove_file(const std::string& path);

// Invoke fn(path, is_dir) for every entry under dir, not including dir.
// Non-recursive. Returns false if dir cannot be opened.
bool list_dir(const std::string& dir, const std::function<void(const std::string& name, bool is_dir)>& fn);

std::string current_dir();
std::string absolute_path(const std::string& path);

// "x64", "x86", "arm64", "arm", "riscv64", "sparc", "ppc64", or "unknown".
const char* arch_name();

// Host directory separator. Archive names always use '/'.
char native_sep();

// Wildcard match, shell style ('*', '?', '[...]'). '/' is a separator and
// is not matched by '*'. Case folding is the caller's job.
bool wild_match(const char* pattern, const char* text);

// Unix permission bits (0777) of a path, following the same rules as lstat
// (do not follow the final symlink). Returns fallback when the host cannot
// report a mode (Windows files, missing path).
uint32_t file_mode(const std::string& path, uint32_t fallback);

// Set Unix permission bits. No-op (returns true) on hosts without modes.
bool set_file_mode(const std::string& path, uint32_t mode);

// Set mtime/atime from a Unix timestamp. Best-effort; false if unsupported.
bool set_mtime(const std::string& path, int64_t unix_seconds);

// Read a password from the terminal with echo disabled when the host allows
// it. Always writes a trailing newline to stderr after a hidden read.
std::string read_password(const char* prompt);

// Local calendar time, "%Y-%m-%d %H:%M:%S". Empty-looking padding if t <= 0.
std::string format_local_time(int64_t unix_seconds);

}  // namespace port
}  // namespace zip67
