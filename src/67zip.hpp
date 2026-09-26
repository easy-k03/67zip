#pragma once

#include <cstdint>
#include <ctime>
#include <string>
#include <vector>
#include <map>
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <functional>
#include <memory>
#include <stdexcept>
#include <cstring>
#include <cerrno>
#include <chrono>
#include <system_error>
#if !defined(ZIP67_NO_FILESYSTEM)
#  include <filesystem>
#endif
#if !defined(ZIP67_NO_THREADS)
#  include <thread>
#endif
#include <mutex>
#include <atomic>
#include <optional>

#include <zlib.h>
#include <lzma.h>

#if !defined(ZIP67_NO_FILESYSTEM)
namespace fs = std::filesystem;
#endif

namespace zip67 {

// 67z container magic: "67ZIP" + version byte 1 + flags
static constexpr char kMagic[6] = {'6', '7', 'Z', 'I', 'P', '\x01'};
static constexpr uint8_t kVersion = 1;

enum class Method : uint8_t {
    Store = 0,
    Deflate = 1,
    Lzma2 = 2,
};

enum class EntryType : uint8_t {
    File = 0,
    Directory = 1,
    Symlink = 2,
};

struct Entry {
    std::string name;          // archive-relative path, '/' separators, no leading '/'
    EntryType type = EntryType::File;
    Method method = Method::Lzma2;
    uint64_t size = 0;         // uncompressed
    uint64_t packed = 0;       // compressed payload size
    uint64_t offset = 0;       // payload offset from start of file
    uint32_t crc32 = 0;
    uint32_t mode = 0644;  // Unix mode bits, see port.hpp modebit
    int64_t mtime = 0;         // unix seconds
    std::string link;          // symlink target
    bool encrypted = false;    // reserved; password is a key check, not a cipher
};

struct Archive {
    std::string path;
    std::vector<Entry> entries;
    uint32_t flags = 0;
    bool solid = false;
    std::string comment;
};

struct Options {
    std::string command;
    std::string archive;
    std::vector<std::string> files;
    std::vector<std::string> include;
    std::vector<std::string> exclude;
    std::vector<std::string> include_archives;
    std::vector<std::string> exclude_archives;
    bool recurse = false;
    int recurse_mode = 1;      // -r / -r- / -r0
    int level = 5;             // -mx
    int threads = 0;           // -mmt, 0 = auto
    Method method = Method::Lzma2;
    std::string type = "67z";  // -t
    std::string out_dir;       // -o
    std::string password;      // -p
    bool password_set = false;
    bool ask_password = false;
    char overwrite = 'a';      // -ao a|s|t|u   a=ask s=skip t=rename u=overwrite
    bool yes = false;          // -y
    bool list_tech = false;    // -slt
    bool delete_after = false; // -sdel
    bool to_stdout = false;    // -so
    bool from_stdin = false;   // -si
    std::string stdin_name;
    bool case_sensitive = true;
    bool stop_on_error = false; // -sse
    int log_level = 1;         // -bb
    bool no_progress = false;  // -bd
    bool show_time = false;    // -bt
    std::string hash_name = "CRC32"; // -scrc
    std::string work_dir;
    uint64_t volume = 0;       // -v (stored as note; multi-volume is sequential parts)
    bool update_mode = false;
    std::string new_archive;   // -u!name
    bool disable_archive_name = false; // -an
    bool spd = false;          // disable wildcards
    bool spe = false;
    int sa = 'a';              // archive name mode a|e|s
    std::string sfx;
    std::vector<std::pair<std::string, std::string>> renames; // rn
    bool quiet_stdout = false;
    int bs_out = 1;
    int bs_err = 1;
    int bs_prog = 1;
};

enum ExitCode {
    kOk = 0,
    kWarning = 1,
    kFatal = 2,
    kCmdError = 7,
    kOom = 8,
    kUserStop = 255,
};

std::string method_name(Method m);
std::string type_name(EntryType t);
uint32_t crc32_bytes(const void* data, size_t n, uint32_t crc = 0);
uint32_t crc32_file(const std::string& path, std::string* err);

bool compress_buffer(Method m, int level, const uint8_t* in, size_t in_n,
                     std::vector<uint8_t>& out, std::string& err);
bool decompress_buffer(Method m, const uint8_t* in, size_t in_n,
                       uint64_t expect, std::vector<uint8_t>& out, std::string& err);

bool write_archive(const std::string& path, const std::vector<Entry>& entries,
                   const std::map<std::string, std::vector<uint8_t>>& payloads,
                   const std::string& password, std::string& err);
bool read_archive(const std::string& path, Archive& arc,
                  std::map<std::string, std::vector<uint8_t>>* payloads,
                  const std::string& password, std::string& err);

int run(int argc, char** argv);

}  // namespace zip67
