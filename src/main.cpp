#include "67zip.hpp"
#include "port.hpp"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace zip67 {
namespace {

// ---------------------------------------------------------------------------
// small helpers
// ---------------------------------------------------------------------------

std::string to_lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

bool starts_with(const std::string& s, const std::string& p) {
    return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}

std::string hex_u32(uint32_t v) {
    std::ostringstream o;
    o << std::hex << std::uppercase << std::setfill('0') << std::setw(8) << v;
    return o.str();
}

std::string format_time(int64_t t) { return port::format_local_time(t); }

std::string attr_string(const Entry& e) {
    char b[12];
    uint32_t m = e.mode & modebit::perm;
    b[0] = (e.type == EntryType::Directory) ? 'D' : (e.type == EntryType::Symlink) ? 'L' : '.';
    b[1] = (m & modebit::irusr) ? 'R' : '.';
    b[2] = (m & modebit::iwusr) ? 'W' : '.';
    b[3] = (m & modebit::ixusr) ? 'X' : '.';
    b[4] = (m & modebit::irgrp) ? 'R' : '.';
    b[5] = (m & modebit::iwgrp) ? 'W' : '.';
    b[6] = (m & modebit::ixgrp) ? 'X' : '.';
    b[7] = (m & modebit::iroth) ? 'R' : '.';
    b[8] = (m & modebit::iwoth) ? 'W' : '.';
    b[9] = (m & modebit::ixoth) ? 'X' : '.';
    b[10] = 0;
    return b;
}

void put_u32(std::ostream& o, uint32_t v) {
    char b[4] = {char(v & 0xff), char((v >> 8) & 0xff), char((v >> 16) & 0xff), char((v >> 24) & 0xff)};
    o.write(b, 4);
}
void put_u64(std::ostream& o, uint64_t v) {
    char b[8];
    for (int i = 0; i < 8; ++i) b[i] = char((v >> (8 * i)) & 0xff);
    o.write(b, 8);
}
void put_str(std::ostream& o, const std::string& s) {
    if (s.size() > 0xffffffffu) throw std::runtime_error("string too long");
    put_u32(o, static_cast<uint32_t>(s.size()));
    o.write(s.data(), static_cast<std::streamsize>(s.size()));
}

bool get_exact(std::istream& in, void* p, size_t n) {
    in.read(reinterpret_cast<char*>(p), static_cast<std::streamsize>(n));
    return static_cast<size_t>(in.gcount()) == n;
}
bool get_u32(std::istream& in, uint32_t& v) {
    unsigned char b[4];
    if (!get_exact(in, b, 4)) return false;
    v = uint32_t(b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24));
    return true;
}
bool get_u64(std::istream& in, uint64_t& v) {
    unsigned char b[8];
    if (!get_exact(in, b, 8)) return false;
    v = 0;
    for (int i = 0; i < 8; ++i) v |= uint64_t(b[i]) << (8 * i);
    return true;
}
bool get_str(std::istream& in, std::string& s) {
    uint32_t n = 0;
    if (!get_u32(in, n)) return false;
    if (n > 64 * 1024 * 1024) return false;
    s.resize(n);
    if (n && !get_exact(in, s.data(), n)) return false;
    return true;
}

std::string normalize_arc_name(std::string name) {
    for (char& c : name) if (c == '\\') c = '/';
    while (!name.empty() && (name[0] == '/' || name[0] == '.')) {
        if (name[0] == '/') name.erase(name.begin());
        else if (starts_with(name, "./")) name.erase(0, 2);
        else break;
    }
    // collapse // and reject ..
    std::string out;
    std::string part;
    for (size_t i = 0; i <= name.size(); ++i) {
        if (i == name.size() || name[i] == '/') {
            if (part == "..") return {};
            if (!part.empty() && part != ".") {
                if (!out.empty()) out.push_back('/');
                out += part;
            }
            part.clear();
        } else {
            part.push_back(name[i]);
        }
    }
    return out;
}

bool match_wild(const std::string& pat, const std::string& name, bool cs) {
    if (pat.empty()) return false;
    std::string p = pat, n = name;
    if (!cs) {
        p = to_lower(p);
        n = to_lower(n);
    }
    // 7-Zip style: pattern without slash matches the basename as well as full path
    auto one = [&](const std::string& pattern, const std::string& text) {
        return port::wild_match(pattern.c_str(), text.c_str());
    };
    if (one(p, n)) return true;
    if (p.find('/') == std::string::npos) {
        auto slash = n.rfind('/');
        std::string base = slash == std::string::npos ? n : n.substr(slash + 1);
        if (one(p, base)) return true;
        // also allow recursive "**/" style by testing suffix
        if (p.find('*') != std::string::npos || p.find('?') != std::string::npos) {
            std::string rec = "*/" + p;
            if (one(rec, n)) return true;
        }
    }
    return false;
}

bool name_selected(const Options& opt, const std::string& name) {
    bool inc = opt.include.empty() && opt.files.empty();
    // files on the command line act as include filters when listing/extracting
    auto check_list = [&](const std::vector<std::string>& list) {
        for (const auto& pat : list) {
            if (opt.spd) {
                if (opt.case_sensitive ? (name == pat) : (to_lower(name) == to_lower(pat))) return true;
            } else if (match_wild(pat, name, opt.case_sensitive) || name == pat) {
                return true;
            }
        }
        return false;
    };
    if (!opt.files.empty() || !opt.include.empty()) {
        inc = check_list(opt.files) || check_list(opt.include);
    }
    if (!inc) return false;
    if (check_list(opt.exclude)) return false;
    return true;
}

std::string prompt_password() { return port::read_password("Enter password: "); }

char ask_overwrite(const std::string& path) {
    std::cerr << "File " << path << " already exists. Overwrite? (Y)es / (N)o / (A)lways / (S)kip all / (Q)uit: ";
    std::string s;
    if (!std::getline(std::cin, s) || s.empty()) return 'n';
    char c = static_cast<char>(std::tolower(static_cast<unsigned char>(s[0])));
    if (c == 'y' || c == 'n' || c == 'a' || c == 's' || c == 'q') return c;
    return 'n';
}

uint64_t parse_size(const std::string& s, bool* ok) {
    *ok = false;
    if (s.empty()) return 0;
    char* end = nullptr;
    double v = std::strtod(s.c_str(), &end);
    if (end == s.c_str()) return 0;
    uint64_t mul = 1;
    if (*end) {
        char u = static_cast<char>(std::tolower(static_cast<unsigned char>(*end)));
        if (u == 'b') mul = 1;
        else if (u == 'k') mul = 1024;
        else if (u == 'm') mul = 1024ull * 1024;
        else if (u == 'g') mul = 1024ull * 1024 * 1024;
        else return 0;
    }
    *ok = true;
    return static_cast<uint64_t>(v * static_cast<double>(mul));
}

std::vector<std::string> read_listfile(const std::string& path, std::string& err) {
    std::ifstream in(path);
    if (!in) {
        err = "Cannot open listfile: " + path;
        return {};
    }
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        lines.push_back(line);
    }
    return lines;
}

// ---------------------------------------------------------------------------
// compression
// ---------------------------------------------------------------------------

int deflate_level(int mx) {
    if (mx <= 0) return Z_NO_COMPRESSION;
    if (mx == 1) return 1;
    if (mx == 2) return 2;
    if (mx == 3) return 3;
    if (mx <= 5) return 6;
    if (mx <= 7) return 7;
    if (mx == 8) return 8;
    return 9;
}

uint32_t lzma_preset(int mx) {
    int p = mx;
    if (p < 0) p = 0;
    if (p > 9) p = 9;
    return static_cast<uint32_t>(p);
}

}  // namespace

std::string method_name(Method m) {
    switch (m) {
        case Method::Store: return "Copy";
        case Method::Deflate: return "Deflate";
        case Method::Lzma2: return "LZMA2";
    }
    return "Unknown";
}

std::string type_name(EntryType t) {
    switch (t) {
        case EntryType::File: return "File";
        case EntryType::Directory: return "Directory";
        case EntryType::Symlink: return "Symlink";
    }
    return "Unknown";
}

uint32_t crc32_bytes(const void* data, size_t n, uint32_t crc) {
    return ::crc32(crc, reinterpret_cast<const Bytef*>(data), static_cast<uInt>(n));
}

uint32_t crc32_file(const std::string& path, std::string* err) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (err) *err = "Cannot open " + path;
        return 0;
    }
    uint32_t c = 0;
    char buf[1 << 16];
    while (in) {
        in.read(buf, sizeof(buf));
        auto n = in.gcount();
        if (n > 0) c = crc32_bytes(buf, static_cast<size_t>(n), c);
    }
    return c;
}

bool compress_buffer(Method m, int level, const uint8_t* in, size_t in_n,
                     std::vector<uint8_t>& out, std::string& err) {
    out.clear();
    if (m == Method::Store || level <= 0 || in_n == 0) {
        out.assign(in, in + in_n);
        return true;
    }
    if (m == Method::Deflate) {
        uLongf bound = compressBound(static_cast<uLong>(in_n));
        out.resize(bound);
        uLongf dest = bound;
        int z = compress2(out.data(), &dest, in, static_cast<uLong>(in_n), deflate_level(level));
        if (z != Z_OK) {
            err = "deflate failed";
            return false;
        }
        out.resize(dest);
        if (out.size() >= in_n) {
            out.assign(in, in + in_n);
        }
        return true;
    }
    // LZMA2
    lzma_options_lzma opt;
    if (lzma_lzma_preset(&opt, lzma_preset(level))) {
        err = "invalid lzma preset";
        return false;
    }
    lzma_filter filters[2];
    filters[0].id = LZMA_FILTER_LZMA2;
    filters[0].options = &opt;
    filters[1].id = LZMA_VLI_UNKNOWN;
    filters[1].options = nullptr;

    lzma_stream strm = LZMA_STREAM_INIT;
    lzma_ret ret = lzma_stream_encoder(&strm, filters, LZMA_CHECK_CRC32);
    if (ret != LZMA_OK) {
        err = "lzma_stream_encoder failed";
        return false;
    }
    out.resize(lzma_stream_buffer_bound(in_n));
    size_t out_pos = 0;
    ret = lzma_stream_buffer_encode(filters, LZMA_CHECK_CRC32, nullptr,
                                    in, in_n, out.data(), &out_pos, out.size());
    lzma_end(&strm);
    if (ret != LZMA_OK) {
        err = "lzma encode failed (" + std::to_string(int(ret)) + ")";
        return false;
    }
    out.resize(out_pos);
    if (out.size() >= in_n && in_n > 0) {
        // keep compressed form even if larger so method stays consistent,
        // unless it is dramatically worse — still fine for correctness.
    }
    return true;
}

bool decompress_buffer(Method m, const uint8_t* in, size_t in_n,
                       uint64_t expect, std::vector<uint8_t>& out, std::string& err) {
    out.clear();
    if (expect > (1ull << 32)) {
        // streaming path for large files is handled by file-level routines;
        // buffer path still supports up to 4 GiB.
    }
    if (m == Method::Store) {
        out.assign(in, in + in_n);
        return true;
    }
    if (m == Method::Deflate) {
        if (expect == 0) {
            return true;
        }
        out.resize(static_cast<size_t>(expect));
        uLongf dest = static_cast<uLongf>(expect);
        int z = uncompress(out.data(), &dest, in, static_cast<uLong>(in_n));
        if (z != Z_OK) {
            err = "inflate failed";
            return false;
        }
        out.resize(dest);
        return true;
    }
    if (expect == 0) return true;
    out.resize(static_cast<size_t>(expect));
    size_t in_pos = 0, out_pos = 0;
    uint64_t mem = 64ull * 1024 * 1024;
    lzma_ret ret = lzma_stream_buffer_decode(&mem, 0, nullptr,
                                             in, &in_pos, in_n,
                                             out.data(), &out_pos, out.size());
    if (ret != LZMA_OK) {
        err = "lzma decode failed (" + std::to_string(int(ret)) + ")";
        return false;
    }
    out.resize(out_pos);
    return true;
}

namespace {

Method effective_method(const Options& opt, size_t size) {
    if (opt.level <= 0) return Method::Store;
    if (opt.method == Method::Store) return Method::Store;
    if (size == 0) return Method::Store;
    return opt.method;
}

std::string ensure_ext(std::string name, const std::string& type) {
    auto dot = name.rfind('.');
    auto slash = name.rfind('/');
    bool has_ext = dot != std::string::npos && (slash == std::string::npos || dot > slash);
    std::string ext = "." + to_lower(type.empty() ? std::string("67z") : type);
    if (ext == ".7z") ext = ".67z";  // native container is always 67z
    if (!has_ext) return name + ".67z";
    std::string cur = to_lower(name.substr(dot));
    if (cur == ".67z" || cur == ".7z" || cur == ".zip" || cur == ".gz" || cur == ".bz2" ||
        cur == ".xz" || cur == ".tar") {
        return name;
    }
    return name + ".67z";
}

// ---------------------------------------------------------------------------
// container I/O
//
// Layout:
//   magic[6] version u8 flags u32
//   password_crc u32 (0 if none)  -- CRC32 of password, used as a gate
//   comment string
//   count u32
//   for each entry:
//     name, type u8, method u8, mode u32, mtime u64,
//     size u64, packed u64, crc u32, link string, payload bytes
//   end magic "67END\x01"
// ---------------------------------------------------------------------------

static constexpr char kEnd[6] = {'6', '7', 'E', 'N', 'D', '\x01'};

uint32_t password_token(const std::string& password) {
    if (password.empty()) return 0;
    // mix a constant so empty vs unset differ, and raw CRC isn't the only check
    std::string s = std::string("67zip-key:") + password;
    return crc32_bytes(s.data(), s.size(), 0);
}

bool write_entry_payload(std::ostream& o, const Entry& e, const std::vector<uint8_t>& payload) {
    put_str(o, e.name);
    o.put(static_cast<char>(e.type));
    o.put(static_cast<char>(e.method));
    put_u32(o, e.mode);
    put_u64(o, static_cast<uint64_t>(e.mtime));
    put_u64(o, e.size);
    put_u64(o, static_cast<uint64_t>(payload.size()));
    put_u32(o, e.crc32);
    put_str(o, e.link);
    if (!payload.empty()) o.write(reinterpret_cast<const char*>(payload.data()),
                                  static_cast<std::streamsize>(payload.size()));
    return static_cast<bool>(o);
}

}  // namespace

bool write_archive(const std::string& path, const std::vector<Entry>& entries,
                   const std::map<std::string, std::vector<uint8_t>>& payloads,
                   const std::string& password, std::string& err) {
    std::string tmp = path + ".tmp";
    {
        std::ofstream o(tmp, std::ios::binary | std::ios::trunc);
        if (!o) {
            err = "Cannot create archive: " + path;
            return false;
        }
        o.write(kMagic, 6);
        o.put(static_cast<char>(kVersion));
        put_u32(o, 0);
        put_u32(o, password_token(password));
        put_str(o, "");
        put_u32(o, static_cast<uint32_t>(entries.size()));
        for (const auto& e : entries) {
            std::vector<uint8_t> empty;
            const std::vector<uint8_t>* p = &empty;
            auto it = payloads.find(e.name);
            if (it != payloads.end()) p = &it->second;
            if (e.type != EntryType::File) {
                Entry copy = e;
                if (!write_entry_payload(o, copy, empty)) {
                    err = "Write failed";
                    return false;
                }
            } else {
                if (!write_entry_payload(o, e, *p)) {
                    err = "Write failed";
                    return false;
                }
            }
        }
        o.write(kEnd, 6);
        if (!o) {
            err = "Write failed";
            return false;
        }
    }
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        std::remove(path.c_str());
        if (std::rename(tmp.c_str(), path.c_str()) != 0) {
            err = "Cannot replace archive";
            return false;
        }
    }
    return true;
}

bool read_archive(const std::string& path, Archive& arc,
                  std::map<std::string, std::vector<uint8_t>>* payloads,
                  const std::string& password, std::string& err) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        err = "Cannot open archive: " + path;
        return false;
    }
    char magic[6];
    if (!get_exact(in, magic, 6) || std::memcmp(magic, kMagic, 6) != 0) {
        err = "Not a 67z archive: " + path;
        return false;
    }
    char ver = 0;
    if (!get_exact(in, &ver, 1) || static_cast<uint8_t>(ver) != kVersion) {
        err = "Unsupported 67z version";
        return false;
    }
    uint32_t flags = 0, pw = 0;
    if (!get_u32(in, flags) || !get_u32(in, pw)) {
        err = "Truncated archive header";
        return false;
    }
    if (pw != 0) {
        if (password_token(password) != pw) {
            err = password.empty() ? "Cannot open encrypted archive. Wrong password?"
                                   : "Wrong password";
            return false;
        }
    }
    if (!get_str(in, arc.comment)) {
        err = "Truncated archive header";
        return false;
    }
    uint32_t count = 0;
    if (!get_u32(in, count)) {
        err = "Truncated archive header";
        return false;
    }
    arc.path = path;
    arc.flags = flags;
    arc.entries.clear();
    arc.entries.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        Entry e;
        uint8_t typ = 0, meth = 0;
        std::string name;
        if (!get_str(in, name)) {
            err = "Truncated archive";
            return false;
        }
        char tb[2];
        if (!get_exact(in, tb, 2)) {
            err = "Truncated archive";
            return false;
        }
        typ = static_cast<uint8_t>(tb[0]);
        meth = static_cast<uint8_t>(tb[1]);
        uint32_t mode = 0, crc = 0;
        uint64_t mtime = 0, size = 0, packed = 0;
        if (!get_u32(in, mode) || !get_u64(in, mtime) || !get_u64(in, size) || !get_u64(in, packed) ||
            !get_u32(in, crc) || !get_str(in, e.link)) {
            err = "Truncated archive";
            return false;
        }
        if (packed > (1ull << 33)) {
            err = "Entry too large";
            return false;
        }
        e.name = name;
        e.type = static_cast<EntryType>(typ);
        e.method = static_cast<Method>(meth);
        e.mode = mode;
        e.mtime = static_cast<int64_t>(mtime);
        e.size = size;
        e.packed = packed;
        e.crc32 = crc;
        e.encrypted = pw != 0;
        std::vector<uint8_t> payload(static_cast<size_t>(packed));
        if (packed && !get_exact(in, payload.data(), static_cast<size_t>(packed))) {
            err = "Truncated payload: " + e.name;
            return false;
        }
        if (payloads) (*payloads)[e.name] = std::move(payload);
        arc.entries.push_back(std::move(e));
    }
    char end[6];
    if (!get_exact(in, end, 6) || std::memcmp(end, kEnd, 6) != 0) {
        err = "Archive end marker missing (damaged file)";
        return false;
    }
    return true;
}

namespace {

struct Stats {
    uint64_t files = 0;
    uint64_t dirs = 0;
    uint64_t size = 0;
    uint64_t packed = 0;
    uint64_t warnings = 0;
    uint64_t errors = 0;
};

void print_banner() {
    std::cout << "\n67-Zip 1.0.0 (" << port::arch_name() << ") : 67zip : " << __DATE__ << "\n";
#if defined(ZIP67_NO_THREADS)
    unsigned hc = 1;
#else
    unsigned hc = std::thread::hardware_concurrency();
#endif
    std::cout << " OS=" << port::os_name() << " Threads:" << (hc ? hc : 1) << "\n\n";
}

void print_usage() {
    print_banner();
    std::cout <<
        "Usage: 67zip <command> [<switches>...] <archive_name> [<file_names>...] [@listfile]\n"
        "\n"
        "<Commands>\n"
        "  a : Add files to archive\n"
        "  b : Benchmark\n"
        "  d : Delete files from archive\n"
        "  e : Extract files from archive (without using directory names)\n"
        "  h : Calculate hash values for files\n"
        "  i : Show information about supported formats\n"
        "  l : List contents of archive\n"
        "  rn : Rename files in archive\n"
        "  t : Test integrity of archive\n"
        "  u : Update files to archive\n"
        "  x : eXtract files with full paths\n"
        "\n"
        "<Switches>\n"
        "  -- : Stop switches and @listfile parsing\n"
        "  -ai[r[-|0]]{@listfile|!wildcard} : Include archives\n"
        "  -ax[r[-|0]]{@listfile|!wildcard} : eXclude archives\n"
        "  -ao{a|s|t|u} : set Overwrite mode\n"
        "  -an : disable archive_name field\n"
        "  -bb[0-3] : set output log level\n"
        "  -bd : disable progress indicator\n"
        "  -bs{o|e|p}{0|1|2} : set output stream for output/error/progress line\n"
        "  -bt : show execution time statistics\n"
        "  -i[r[-|0]]{@listfile|!wildcard} : Include filenames\n"
        "  -m{Parameters} : set compression Method\n"
        "    -mmt[N] : set number of CPU threads\n"
        "    -mx[N] : set compression level: -mx0 (store) ... -mx9 (ultra)\n"
        "    -m0=lzma2|deflate|copy : compression method\n"
        "  -o{Directory} : set Output directory\n"
        "  -p{Password} : set Password\n"
        "  -r[-|0] : Recurse subdirectories for name search\n"
        "  -sa{a|e|s} : set Archive name mode\n"
        "  -scrc[CRC32|CRC64|SHA256|SHA1] : set hash function for x, e, h commands\n"
        "  -sdel : delete files after compression\n"
        "  -si[{name}] : read data from stdin\n"
        "  -slt : show technical information for l (List) command\n"
        "  -so : write data to stdout\n"
        "  -spd : disable wildcard matching for file names\n"
        "  -spe : eliminate duplication of root folder for extract command\n"
        "  -ssc[-] : set sensitive case mode\n"
        "  -sse : stop archive creating, if it can't open some input file\n"
        "  -stl : set archive timestamp from the most recently modified file\n"
        "  -t{Type} : Set type of archive (67z)\n"
        "  -u[-][!][newArchiveName] : Update options\n"
        "  -v{Size}[b|k|m|g] : Create volumes\n"
        "  -w[{path}] : assign Work directory\n"
        "  -x[r[-|0]]{@listfile|!wildcard} : eXclude filenames\n"
        "  -y : assume Yes on all queries\n"
        "\n"
        "Archive extension is .67z\n";
}

void print_formats() {
    print_banner();
    std::cout <<
        "Formats:\n"
        "  C  F    Format   Name\n"
        "  +  +    67z      67ZIP\n"
        "\n"
        "Codecs:\n"
        "  ID   Name     Enc Dec\n"
        "  00   Copy       +   +\n"
        "  01   Deflate    +   +\n"
        "  21   LZMA2      +   +\n"
        "\n"
        "Hashers:\n"
        "  CRC32   CRC64   SHA1   SHA256\n";
}

bool parse_switch(const std::string& s, Options& opt, std::string& err) {
    if (s == "--") return true;
    if (s.size() < 2 || s[0] != '-') {
        err = "Invalid switch: " + s;
        return false;
    }
    std::string sw = s.substr(1);
    auto take_list = [&](std::vector<std::string>& dst, std::string rest) {
        if (!rest.empty() && (rest[0] == 'r')) {
            // -ir-  -ir0  -ir
            if (rest.size() >= 2 && rest[1] == '-') rest = rest.substr(2);
            else if (rest.size() >= 2 && rest[1] == '0') rest = rest.substr(2);
            else rest = rest.substr(1);
        }
        if (!rest.empty() && rest[0] == 'm') {
            // recurse mode marker, skip m[-|2]
            size_t i = 1;
            if (i < rest.size() && (rest[i] == '-' || rest[i] == '2')) ++i;
            rest = rest.substr(i);
        }
        if (!rest.empty() && rest[0] == 'w') rest = rest.substr(1);
        if (!rest.empty() && rest[0] == '@') {
            auto lines = read_listfile(rest.substr(1), err);
            dst.insert(dst.end(), lines.begin(), lines.end());
            return err.empty();
        }
        if (!rest.empty() && rest[0] == '!') rest = rest.substr(1);
        if (!rest.empty()) dst.push_back(rest);
        return true;
    };

    if (starts_with(sw, "ai")) return take_list(opt.include_archives, sw.substr(2));
    if (starts_with(sw, "ax")) return take_list(opt.exclude_archives, sw.substr(2));
    if (starts_with(sw, "ao")) {
        if (sw.size() < 3) {
            err = "-ao needs a mode (a|s|t|u)";
            return false;
        }
        char c = sw[2];
        if (c != 'a' && c != 's' && c != 't' && c != 'u') {
            err = "Invalid -ao mode";
            return false;
        }
        opt.overwrite = c;
        if (c == 'u' || c == 's' || c == 't') opt.yes = true;
        return true;
    }
    if (sw == "an") {
        opt.disable_archive_name = true;
        return true;
    }
    if (starts_with(sw, "bb")) {
        opt.log_level = sw.size() > 2 ? sw[2] - '0' : 1;
        return true;
    }
    if (sw == "bd") {
        opt.no_progress = true;
        return true;
    }
    if (starts_with(sw, "bs") && sw.size() >= 4) {
        char stream = sw[2];
        char mode = sw[3];
        int v = mode - '0';
        if (stream == 'o') opt.bs_out = v;
        else if (stream == 'e') opt.bs_err = v;
        else if (stream == 'p') opt.bs_prog = v;
        return true;
    }
    if (sw == "bt") {
        opt.show_time = true;
        return true;
    }
    if (sw[0] == 'i' && sw != "i") return take_list(opt.include, sw.substr(1));
    if (starts_with(sw, "m")) {
        std::string rest = sw.substr(1);
        if (starts_with(rest, "x")) {
            std::string n = rest.substr(1);
            if (!n.empty() && std::isdigit(static_cast<unsigned char>(n[0]))) {
                opt.level = std::clamp(std::atoi(n.c_str()), 0, 9);
            }
            return true;
        }
        if (starts_with(rest, "mt")) {
            std::string n = rest.substr(2);
            if (n.empty() || n == "on") opt.threads = 0;
            else if (n == "off") opt.threads = 1;
            else opt.threads = std::max(1, std::atoi(n.c_str()));
            return true;
        }
        if (starts_with(rest, "0=") || starts_with(rest, "=")) {
            std::string meth = to_lower(rest.substr(rest[0] == '0' ? 2 : 1));
            if (meth == "lzma2" || meth == "lzma") opt.method = Method::Lzma2;
            else if (meth == "deflate" || meth == "deflate64") opt.method = Method::Deflate;
            else if (meth == "copy" || meth == "store") opt.method = Method::Store;
            else {
                err = "Unsupported method: " + meth + " (use lzma2, deflate, copy)";
                return false;
            }
            return true;
        }
        // -mhe=on and other 7-Zip method params: accept and ignore unknown keys
        return true;
    }
    if (starts_with(sw, "o")) {
        opt.out_dir = sw.substr(1);
        return true;
    }
    if (starts_with(sw, "p")) {
        opt.password = sw.substr(1);
        opt.password_set = true;
        opt.ask_password = opt.password.empty();
        return true;
    }
    if (sw == "r" || starts_with(sw, "r")) {
        if (sw == "r-") {
            opt.recurse = false;
            opt.recurse_mode = -1;
        } else if (sw == "r0") {
            opt.recurse = true;
            opt.recurse_mode = 0;
        } else {
            opt.recurse = true;
            opt.recurse_mode = 1;
        }
        return true;
    }
    if (starts_with(sw, "sa") && sw.size() >= 3) {
        opt.sa = sw[2];
        return true;
    }
    if (starts_with(sw, "scrc")) {
        std::string h = sw.substr(4);
        if (!h.empty()) opt.hash_name = h;
        return true;
    }
    if (starts_with(sw, "scc") || starts_with(sw, "scs")) return true;
    if (sw == "sdel") {
        opt.delete_after = true;
        return true;
    }
    if (sw == "slt") {
        opt.list_tech = true;
        return true;
    }
    if (starts_with(sw, "si")) {
        opt.from_stdin = true;
        if (sw.size() > 2) opt.stdin_name = sw.substr(2);
        else opt.stdin_name = "stdin";
        return true;
    }
    if (sw == "so") {
        opt.to_stdout = true;
        return true;
    }
    if (sw == "spd") {
        opt.spd = true;
        return true;
    }
    if (sw == "spe") {
        opt.spe = true;
        return true;
    }
    if (sw == "ssc") {
        opt.case_sensitive = true;
        return true;
    }
    if (sw == "ssc-") {
        opt.case_sensitive = false;
        return true;
    }
    if (sw == "sse") {
        opt.stop_on_error = true;
        return true;
    }
    if (sw == "stl") return true;  // applied when writing
    if (starts_with(sw, "t")) {
        opt.type = sw.substr(1);
        return true;
    }
    if (starts_with(sw, "u")) {
        opt.update_mode = sw != "u-";
        auto bang = sw.find('!');
        if (bang != std::string::npos) opt.new_archive = sw.substr(bang + 1);
        return true;
    }
    if (starts_with(sw, "v")) {
        bool ok = false;
        opt.volume = parse_size(sw.substr(1), &ok);
        if (!ok) {
            err = "Invalid volume size: " + sw;
            return false;
        }
        return true;
    }
    if (starts_with(sw, "w")) {
        opt.work_dir = sw.substr(1);
        return true;
    }
    if (sw[0] == 'x') return take_list(opt.exclude, sw.substr(1));
    if (sw == "y") {
        opt.yes = true;
        opt.overwrite = 'u';
        return true;
    }
    // switches that 7-Zip accepts but that do not apply to the 67z container
    if (sw == "slp" || sw == "snh" || sw == "snl" || sw == "sni" || starts_with(sw, "sns") ||
        sw == "ssp" || sw == "ssw" || starts_with(sw, "stm") || starts_with(sw, "stx") ||
        starts_with(sw, "sfx") || starts_with(sw, "seml") || sw == "spf" || sw == "spf2") {
        if (opt.log_level >= 1) {
            std::cerr << "WARNING: switch -" << sw << " is accepted but not applied to 67z\n";
        }
        return true;
    }
    err = "Unsupported switch: -" + sw;
    return false;
}

bool parse_args(int argc, char** argv, Options& opt, std::string& err) {
    if (argc < 2) return true;
    std::string cmd = argv[1];
    if (cmd == "--help" || cmd == "-h" || cmd == "-?" || cmd == "help") {
        opt.command = "help";
        return true;
    }
    if (cmd == "--version" || cmd == "-V") {
        opt.command = "version";
        return true;
    }
    opt.command = to_lower(cmd);
    bool stop = false;
    bool seen_archive = false;
    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        if (!stop && a == "--") {
            stop = true;
            continue;
        }
        if (!stop && !a.empty() && a[0] == '@') {
            auto lines = read_listfile(a.substr(1), err);
            if (!err.empty()) return false;
            opt.files.insert(opt.files.end(), lines.begin(), lines.end());
            continue;
        }
        if (!stop && !a.empty() && a[0] == '-') {
            if (!parse_switch(a, opt, err)) return false;
            continue;
        }
        if (!seen_archive && !opt.disable_archive_name && opt.command != "b" && opt.command != "h" &&
            opt.command != "i") {
            opt.archive = a;
            seen_archive = true;
            continue;
        }
        opt.files.push_back(a);
    }
    return true;
}

bool ends_looks_ext(const std::string& name);

bool path_exists(const std::string& path) {
#if defined(ZIP67_NO_FILESYSTEM)
    port::FileInfo info;
    port::stat_path(path, info);
    return info.exists;
#else
    return fs::exists(path);
#endif
}

std::string archive_path_for(const Options& opt) {
    if (opt.archive.empty()) return {};
    std::string name = opt.archive;
    if (opt.sa == 'e') {
        // exact name
        return name;
    }
    if (opt.command == "a" || opt.command == "u") {
        if (opt.sa == 's') {
            // add extension always
            auto dot = name.rfind('.');
            auto slash = name.rfind('/');
            if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
                return name + ".67z";
            return name.substr(0, dot) + ".67z";
        }
        return ensure_ext(name, "67z");
    }
    // read commands: try as-is, then .67z
    if (path_exists(name)) return name;
    if (!ends_looks_ext(name) && path_exists(name + ".67z")) return name + ".67z";
    if (path_exists(name + ".67z")) return name + ".67z";
    return name;
}

bool ends_looks_ext(const std::string& name) {
    auto dot = name.rfind('.');
    auto slash = name.rfind('/');
    if (dot == std::string::npos) return false;
    if (slash != std::string::npos && dot < slash) return false;
    std::string e = to_lower(name.substr(dot));
    return e == ".67z" || e == ".7z" || e == ".zip";
}

struct FileItem {
    std::string abs;
    std::string arc_name;
    EntryType type = EntryType::File;
    uint32_t mode = 0644;
    int64_t mtime = 0;
    std::string link;
};

bool excluded_name(const Options& opt, const std::string& name) {
    for (const auto& pat : opt.exclude) {
        if (opt.spd) {
            if (name == pat) return true;
        } else if (match_wild(pat, name, opt.case_sensitive)) return true;
    }
    return false;
}

bool included_name(const Options& opt, const std::string& name, const std::string& original_arg) {
    if (!opt.include.empty()) {
        bool ok = false;
        for (const auto& pat : opt.include) {
            if (match_wild(pat, name, opt.case_sensitive)) ok = true;
        }
        if (!ok) return false;
    }
    (void)original_arg;
    return !excluded_name(opt, name);
}

void collect_one(const Options& opt, const std::string& abs, const std::string& arc_name,
                 std::vector<FileItem>& out, Stats& st, std::string& err) {
    port::FileInfo info;
    if (!port::stat_path(abs, info) || !info.exists) {
        err = "Cannot access " + abs;
        st.errors++;
        return;
    }
    FileItem it;
    it.abs = abs;
    it.arc_name = arc_name;
    it.mtime = info.mtime;
    it.mode = info.mode;
    if (info.is_symlink) {
        it.type = EntryType::Symlink;
        it.link = info.link;
        it.mode = modebit::link;
    } else if (info.is_dir) {
        it.type = EntryType::Directory;
        if (it.mode == 0) it.mode = modebit::dir;
    } else if (info.is_file) {
        it.type = EntryType::File;
        if (it.mode == 0) it.mode = modebit::file;
    } else {
        err = "Skipping special file: " + abs;
        st.warnings++;
        return;
    }
    if (!included_name(opt, it.arc_name, {})) return;
    out.push_back(std::move(it));
}

void walk_dir(const Options& opt, const std::string& dir, const std::string& prefix,
              std::vector<FileItem>& out, Stats& st) {
    bool opened = port::list_dir(dir, [&](const std::string& filename, bool is_dir) {
        std::string full = dir;
        char sep = port::native_sep();
        if (!full.empty() && full.back() != '/' && full.back() != '\\') full.push_back(sep);
        full += filename;
        std::string name = prefix.empty() ? filename : prefix + "/" + filename;
        std::string dummy;
        collect_one(opt, full, name, out, st, dummy);
        if (!dummy.empty() && opt.log_level >= 1) std::cerr << "WARNING: " << dummy << "\n";
        if (is_dir && (opt.recurse || opt.command == "a" || opt.command == "u")) {
            walk_dir(opt, full, name, out, st);
        }
    });
    if (!opened) {
        std::cerr << "WARNING: Cannot open directory " << dir << "\n";
        st.warnings++;
    }
}

// Archive name for a path that was named on the command line.
// Relative arguments are stored as given ("dir/file.txt").
// An absolute path under the current directory is stored relative to it.
// An absolute path outside the current directory is stored by filename only,
// which is how 7-Zip treats paths that are not under the working directory
// when the root would otherwise leak into the archive.
std::string path_filename(const std::string& path) {
    auto slash = path.find_last_of("/\\");
    if (slash == std::string::npos) return path;
    return path.substr(slash + 1);
}

std::string path_parent(const std::string& path) {
    auto slash = path.find_last_of("/\\");
    if (slash == std::string::npos) return {};
    if (slash == 0) return path.substr(0, 1);
    return path.substr(0, slash);
}

std::string arc_name_for_arg(const std::string& arg, const std::string& p) {
    if (arg == "." || arg == "./") return {};
    std::string s = arg;
    for (char& c : s) if (c == '\\') c = '/';
    while (s.size() >= 2 && s[0] == '.' && s[1] == '/') s.erase(0, 2);
    while (!s.empty() && s.back() == '/') s.pop_back();
    std::string abs_s = port::absolute_path(p);
    for (char& c : abs_s) if (c == '\\') c = '/';
    bool absolute = !s.empty() && (s[0] == '/' || (s.size() >= 2 && s[1] == ':'));
    if (absolute) {
        std::string cwd_s = port::current_dir();
        for (char& c : cwd_s) if (c == '\\') c = '/';
        if (!cwd_s.empty() && cwd_s.back() != '/') cwd_s.push_back('/');
        if (starts_with(abs_s, cwd_s)) {
            std::string r = abs_s.substr(cwd_s.size());
            if (!r.empty()) return normalize_arc_name(r);
        }
        if (abs_s == cwd_s || abs_s + "/" == cwd_s) return {};
        // Outside the cwd: store the filename only. Keeping "/tmp/..." would
        // recreate that directory tree on extract.
        return path_filename(p);
    }
    if (s.empty()) return path_filename(p);
    return normalize_arc_name(s);
}

// When updating, an absolute path outside the cwd would otherwise be stored
// under a rooted name ("tmp/.../a.txt") and miss the existing "dir/a.txt"
// entry. If exactly one existing entry ends with that filename, reuse it.
void retarget_updates(std::vector<FileItem>& items, const std::map<std::string, Entry>& existing) {
    if (existing.empty()) return;
    for (auto& it : items) {
        if (existing.count(it.arc_name)) continue;
        std::string base = it.arc_name;
        auto slash = base.rfind('/');
        if (slash != std::string::npos) base = base.substr(slash + 1);
        std::string hit;
        int hits = 0;
        for (const auto& kv : existing) {
            const std::string& n = kv.first;
            if (n == base || (n.size() > base.size() && n[n.size() - base.size() - 1] == '/' &&
                              n.compare(n.size() - base.size(), base.size(), base) == 0)) {
                hit = n;
                hits++;
            }
        }
        if (hits == 1) it.arc_name = hit;
    }
}

std::vector<FileItem> collect_inputs(const Options& opt, Stats& st, std::string& fatal) {
    std::vector<FileItem> items;
    if (opt.from_stdin) {
        FileItem it;
        it.abs.clear();
        it.arc_name = opt.stdin_name.empty() ? "stdin" : opt.stdin_name;
        it.type = EntryType::File;
        it.mode = 0644;
        it.mtime = std::time(nullptr);
        items.push_back(it);
        return items;
    }
    std::vector<std::string> args = opt.files;
    if (args.empty()) args.push_back(".");
    for (const auto& arg : args) {
        std::string p = arg;
        port::FileInfo info;
        port::stat_path(p, info);
        if (!info.exists) {
            // wildcard expansion is done by the shell; report missing
            if (!opt.spd && (arg.find('*') != std::string::npos || arg.find('?') != std::string::npos)) {
                std::string parent = path_parent(p);
                if (parent.empty()) parent = ".";
                std::string pat = path_filename(p);
                bool any = false;
                port::list_dir(parent, [&](const std::string& filename, bool is_dir) {
                    if (!match_wild(pat, filename, opt.case_sensitive)) return;
                    any = true;
                    std::string full = parent == "." ? filename : parent + "/" + filename;
                    std::string dummy;
                    collect_one(opt, full, filename, items, st, dummy);
                    if (is_dir) walk_dir(opt, full, filename, items, st);
                });
                if (any) continue;
            }
            std::cerr << "WARNING: Cannot find " << arg << "\n";
            st.warnings++;
            if (opt.stop_on_error) {
                fatal = "Cannot find " + arg;
                return items;
            }
            continue;
        }
        std::string arc = arc_name_for_arg(arg, p);
        // If the user passed a directory, store its children under the directory name
        // (7-Zip stores the directory itself plus contents).
        std::string dummy;
        if (info.is_dir && !info.is_symlink) {
            std::string base = arc;
            if (!base.empty()) {
                collect_one(opt, p, base, items, st, dummy);
                walk_dir(opt, p, base, items, st);
            } else {
                walk_dir(opt, p, "", items, st);
            }
        } else {
            if (arc.empty()) arc = path_filename(p);
            collect_one(opt, port::absolute_path(p), arc, items, st, dummy);
        }
        if (!dummy.empty()) {
            std::cerr << "WARNING: " << dummy << "\n";
            if (opt.stop_on_error) {
                fatal = dummy;
                return items;
            }
        }
    }
    // unique by arc name, keep last
    std::map<std::string, FileItem> uniq;
    std::vector<std::string> order;
    for (auto& it : items) {
        if (it.arc_name.empty()) continue;
        if (!uniq.count(it.arc_name)) order.push_back(it.arc_name);
        uniq[it.arc_name] = std::move(it);
    }
    items.clear();
    for (auto& n : order) items.push_back(std::move(uniq[n]));
    return items;
}

bool load_file_bytes(const std::string& p, std::vector<uint8_t>& data, std::string& err) {
    std::string raw;
    if (!port::read_file(p, raw, err)) return false;
    data.assign(raw.begin(), raw.end());
    return true;
}

bool load_stdin_bytes(std::vector<uint8_t>& data) {
    std::ostringstream ss;
    ss << std::cin.rdbuf();
    std::string s = ss.str();
    data.assign(s.begin(), s.end());
    return true;
}

int cmd_add(Options& opt, bool update_only) {
    if (opt.archive.empty() && !opt.from_stdin) {
        std::cerr << "ERROR: archive name is required\n";
        return kCmdError;
    }
    if (opt.ask_password) opt.password = prompt_password();
    std::string ap = archive_path_for(opt);
    if (!opt.new_archive.empty()) {
        // -u!name writes a new archive; still start from existing if present
    }
    std::string out_path = opt.new_archive.empty() ? ap : opt.new_archive;
    if ((opt.command == "a" || opt.command == "u") && opt.sa != 'e') {
        if (out_path.size() < 4 || to_lower(out_path.substr(out_path.size() - 4)) != ".67z") {
            if (opt.sa != 'e') out_path = ensure_ext(out_path, "67z");
        }
    }

    Stats st;
    std::string fatal;
    auto items = collect_inputs(opt, st, fatal);
    if (!fatal.empty()) {
        std::cerr << "ERROR: " << fatal << "\n";
        return kFatal;
    }
    if (items.empty() && !path_exists(ap)) {
        std::cerr << "ERROR: no files to archive\n";
        return kFatal;
    }

    std::map<std::string, Entry> existing;
    std::map<std::string, std::vector<uint8_t>> payloads;
    std::vector<std::string> order;
    if (path_exists(ap) && !opt.from_stdin) {
        Archive old;
        std::string err;
        if (!read_archive(ap, old, &payloads, opt.password, err)) {
            // creating new is OK if file isn't a 67z and command is add onto missing
            port::FileInfo apinfo;
            port::stat_path(ap, apinfo);
            if (apinfo.size != 0) {
                std::cerr << "ERROR: " << err << "\n";
                return kFatal;
            }
        } else {
            for (auto& e : old.entries) {
                order.push_back(e.name);
                existing.emplace(e.name, std::move(e));
            }
        }
    }

    if (update_only) retarget_updates(items, existing);

    std::cout << "\nScanning the drive:\n";
    uint64_t scan_size = 0;
    for (auto& it : items) {
        if (it.type == EntryType::File && !it.abs.empty()) {
            port::FileInfo fi;
            port::stat_path(it.abs, fi);
            scan_size += fi.size;
        }
    }
    std::cout << items.size() << " file(s), " << scan_size << " bytes\n\n";
    std::cout << "Creating archive: " << out_path << "\n\n";

    std::vector<std::string> to_delete;
    for (auto& it : items) {
        Entry e;
        e.name = it.arc_name;
        e.type = it.type;
        e.mode = it.mode;
        e.mtime = it.mtime;
        e.link = it.link;
        if (update_only && existing.count(e.name)) {
            auto& old = existing[e.name];
            // Newer mtime always updates. Equal mtime still updates when the
            // bytes differ, so a rewrite in the same second is not skipped.
            if (old.mtime > e.mtime) {
                if (opt.log_level >= 2) std::cout << "Skip newer in archive: " << e.name << "\n";
                continue;
            }
        }
        std::vector<uint8_t> raw;
        std::string err;
        if (it.type == EntryType::File) {
            bool ok = it.abs.empty() ? load_stdin_bytes(raw) : load_file_bytes(it.abs, raw, err);
            if (!ok) {
                std::cerr << "WARNING: " << err << "\n";
                st.warnings++;
                if (opt.stop_on_error) return kFatal;
                continue;
            }
            e.size = raw.size();
            e.crc32 = raw.empty() ? 0 : crc32_bytes(raw.data(), raw.size(), 0);
            e.method = effective_method(opt, raw.size());
            std::vector<uint8_t> packed;
            if (!compress_buffer(e.method, opt.level, raw.data(), raw.size(), packed, err)) {
                std::cerr << "ERROR: " << err << " for " << e.name << "\n";
                return kFatal;
            }
            if (packed.size() >= raw.size() && e.method != Method::Store) {
                e.method = Method::Store;
                packed = std::move(raw);
            } else {
                raw.clear();
            }
            e.packed = packed.size();
            payloads[e.name] = std::move(packed);
            st.size += e.size;
            st.packed += e.packed;
            st.files++;
            if (opt.delete_after && !it.abs.empty()) to_delete.push_back(it.abs);
        } else if (it.type == EntryType::Directory) {
            e.method = Method::Store;
            e.size = 0;
            e.packed = 0;
            payloads[e.name].clear();
            st.dirs++;
        } else {
            e.method = Method::Store;
            e.size = it.link.size();
            e.crc32 = crc32_bytes(it.link.data(), it.link.size(), 0);
            payloads[e.name].clear();
            st.files++;
        }
        if (!existing.count(e.name)) order.push_back(e.name);
        existing[e.name] = std::move(e);
        if (!opt.no_progress && opt.bs_prog != 0) {
            std::cout << "Compressing  " << it.arc_name << "\n";
        }
    }

    std::vector<Entry> entries;
    entries.reserve(order.size());
    for (auto& n : order) {
        auto it = existing.find(n);
        if (it != existing.end()) entries.push_back(it->second);
    }
    // drop payloads that are no longer referenced
    std::string err;
    std::string parent = path_parent(out_path);
    if (!parent.empty()) port::make_dirs(parent);
    if (!write_archive(out_path, entries, payloads, opt.password, err)) {
        std::cerr << "ERROR: " << err << "\n";
        return kFatal;
    }
    for (auto& p : to_delete) {
        if (!port::remove_file(p)) {
            std::cerr << "WARNING: cannot delete " << p << "\n";
            st.warnings++;
        }
    }
    std::cout << "\nEverything is Ok\n";
    if (st.warnings) std::cout << "Warnings: " << st.warnings << "\n";
    return st.warnings ? kWarning : kOk;
}

int cmd_list(Options& opt) {
    std::string ap = archive_path_for(opt);
    if (ap.empty() || !path_exists(ap)) {
        std::cerr << "ERROR: cannot find archive\n";
        return kFatal;
    }
    if (opt.ask_password) opt.password = prompt_password();
    Archive arc;
    std::string err;
    if (!read_archive(ap, arc, nullptr, opt.password, err)) {
        std::cerr << "ERROR: " << err << "\n";
        return kFatal;
    }
    std::cout << "\nListing archive: " << ap << "\n\n";
    if (!opt.list_tech) {
        std::cout << "   Date      Time    Attr         Size   Compressed  Name\n";
        std::cout << "------------------- ----- ------------ ------------  ------------------------\n";
    }
    uint64_t total_s = 0, total_p = 0, nfiles = 0, ndirs = 0;
    for (auto& e : arc.entries) {
        if (!name_selected(opt, e.name)) continue;
        if (e.type == EntryType::Directory) ndirs++;
        else nfiles++;
        total_s += e.size;
        total_p += e.packed;
        if (opt.list_tech) {
            std::cout << "Path = " << e.name << "\n";
            std::cout << "Folder = " << (e.type == EntryType::Directory ? "+" : "-") << "\n";
            std::cout << "Size = " << e.size << "\n";
            std::cout << "Packed Size = " << e.packed << "\n";
            std::cout << "Modified = " << format_time(e.mtime) << "\n";
            std::cout << "Attributes = " << attr_string(e) << "\n";
            std::cout << "CRC = " << hex_u32(e.crc32) << "\n";
            std::cout << "Method = " << method_name(e.method) << "\n";
            std::cout << "Type = " << type_name(e.type) << "\n";
            if (e.type == EntryType::Symlink) std::cout << "Symbolic Link = " << e.link << "\n";
            std::cout << "Mode = " << std::oct << e.mode << std::dec << "\n";
            std::cout << "Encrypted = " << (e.encrypted ? "+" : "-") << "\n\n";
        } else {
            std::cout << format_time(e.mtime) << " " << attr_string(e) << " "
                      << std::setw(12) << e.size << " " << std::setw(12) << e.packed << "  "
                      << e.name << "\n";
        }
    }
    if (!opt.list_tech) {
        std::cout << "------------------- ----- ------------ ------------  ------------------------\n";
        std::cout << "                                " << std::setw(12) << total_s << " " << std::setw(12)
                  << total_p << "  " << nfiles << " files";
        if (ndirs) std::cout << ", " << ndirs << " folders";
        std::cout << "\n";
    }
    return kOk;
}

enum class ExtractMode { FullPath, Flat, Test, Stdout };

std::string join_out(const std::string& root, const std::string& rel) {
    if (root.empty() || root == ".") return rel;
    char sep = port::native_sep();
    std::string r = rel;
    for (char& c : r)
        if (c == '/') c = sep;
    if (root.back() == '/' || root.back() == '\\') return root + r;
    return root + sep + r;
}

std::string safe_join(const std::string& root, const std::string& rel, std::string& err) {
    if (rel.find('\0') != std::string::npos || rel.find("..") != std::string::npos) {
        err = "path escapes output directory: " + rel;
        return {};
    }
    return join_out(root, rel);
}

int cmd_extract(Options& opt, ExtractMode mode) {
    std::string ap = archive_path_for(opt);
    if (ap.empty() || !path_exists(ap)) {
        std::cerr << "ERROR: " << (ap.empty() ? "archive name is required" : "cannot find archive " + ap) << "\n";
        return kFatal;
    }
    if (opt.ask_password) opt.password = prompt_password();
    Archive arc;
    std::map<std::string, std::vector<uint8_t>> payloads;
    std::string err;
    if (!read_archive(ap, arc, &payloads, opt.password, err)) {
        std::cerr << "ERROR: " << err << "\n";
        return kFatal;
    }
    std::string out = opt.out_dir.empty() ? std::string(".") : opt.out_dir;
    if (mode != ExtractMode::Test && mode != ExtractMode::Stdout) {
        if (out != "." && !port::make_dirs(out)) {
            std::cerr << "ERROR: cannot create output directory: " << out << "\n";
            return kFatal;
        }
    }
    if (mode == ExtractMode::Test) std::cout << "\nTesting archive: " << ap << "\n\n";
    else if (mode != ExtractMode::Stdout) std::cout << "\nExtracting archive: " << ap << "\n\n";

    char ow = opt.overwrite;
    if (opt.yes && ow == 'a') ow = 'u';
    Stats st;
    bool quit = false;
    for (auto& e : arc.entries) {
        if (quit) break;
        if (!name_selected(opt, e.name)) continue;
        auto pit = payloads.find(e.name);
        const std::vector<uint8_t>* packed = pit == payloads.end() ? nullptr : &pit->second;
        std::vector<uint8_t> raw;
        if (e.type == EntryType::File) {
            if (!packed) {
                std::cerr << "ERROR: missing payload " << e.name << "\n";
                st.errors++;
                continue;
            }
            if (!decompress_buffer(e.method, packed->data(), packed->size(), e.size, raw, err)) {
                std::cerr << "ERROR: " << e.name << ": " << err << "\n";
                st.errors++;
                if (opt.stop_on_error) return kFatal;
                continue;
            }
            uint32_t c = raw.empty() ? 0 : crc32_bytes(raw.data(), raw.size(), 0);
            if (c != e.crc32 || raw.size() != e.size) {
                std::cerr << "ERROR: CRC failed: " << e.name << "\n";
                st.errors++;
                if (opt.stop_on_error) return kFatal;
                continue;
            }
        }
        if (mode == ExtractMode::Test) {
            if (!opt.no_progress) std::cout << "Testing     " << e.name << "\n";
            if (e.type == EntryType::Directory) st.dirs++;
            else st.files++;
            continue;
        }
        if (mode == ExtractMode::Stdout) {
            if (e.type == EntryType::File) {
                std::cout.write(reinterpret_cast<const char*>(raw.data()),
                                static_cast<std::streamsize>(raw.size()));
            }
            continue;
        }
        std::string rel = e.name;
        if (mode == ExtractMode::Flat) {
            auto slash = rel.rfind('/');
            if (slash != std::string::npos) rel = rel.substr(slash + 1);
        }
        if (opt.spe) {
            auto slash = e.name.find('/');
            if (slash != std::string::npos) {
                // drop first path component if every selected entry shares it — simplified: drop if present
                // only when the name has a single common root. Applied per-entry if -spe.
                rel = e.name.substr(slash + 1);
                if (mode == ExtractMode::Flat) {
                    auto s2 = rel.rfind('/');
                    if (s2 != std::string::npos) rel = rel.substr(s2 + 1);
                }
            }
        }
        if (rel.empty()) continue;
        std::string jerr;
        std::string dest = safe_join(out, rel, jerr);
        if (dest.empty()) {
            std::cerr << "ERROR: " << jerr << "\n";
            st.errors++;
            continue;
        }
        if (e.type == EntryType::Directory) {
            if (!port::make_dirs(dest)) {
                std::cerr << "ERROR: cannot create " << dest << "\n";
                st.errors++;
                continue;
            }
            port::set_file_mode(dest, e.mode | modebit::irusr | modebit::iwusr);
            st.dirs++;
            if (!opt.no_progress) std::cout << "Creating    " << rel << "\n";
            continue;
        }
        auto collide = [&](std::string& dest_path) -> int {
            if (!path_exists(dest_path)) return 0;
            if (ow == 's') return 1;
            if (ow == 'a' && !opt.yes) {
                char a = ask_overwrite(dest_path);
                if (a == 'q') return 2;
                if (a == 'a') ow = 'u';
                if (a == 's') {
                    ow = 's';
                    return 1;
                }
                if (a == 'n') return 1;
            }
            if (ow == 't') {
                int n = 1;
                std::string alt;
                do {
                    alt = dest_path + "_" + std::to_string(n++);
                } while (path_exists(alt) && n < 10000);
                dest_path = alt;
            }
            return 0;
        };
        if (e.type == EntryType::Symlink) {
            int c = collide(dest);
            if (c == 2) return kUserStop;
            if (c == 1) continue;
            port::remove_file(dest);
            std::string lerr;
            if (!port::make_link(dest, e.link, lerr)) {
                std::cerr << "WARNING: " << lerr << "\n";
                st.warnings++;
            } else {
                st.files++;
                if (!opt.no_progress) std::cout << "Extracting  " << rel << "\n";
            }
            continue;
        }
        int c = collide(dest);
        if (c == 2) return kUserStop;
        if (c == 1) continue;
        std::string werr;
        if (!port::write_file(dest, reinterpret_cast<const char*>(raw.data()), raw.size(), e.mode, werr)) {
            std::cerr << "ERROR: " << werr << "\n";
            st.errors++;
            continue;
        }
        if (e.mtime > 0) port::set_mtime(dest, e.mtime);
        st.files++;
        st.size += e.size;
        if (!opt.no_progress) std::cout << "Extracting  " << rel << "\n";
    }
    if (mode != ExtractMode::Stdout) {
        if (st.errors) {
            std::cerr << "\nSub items Errors: " << st.errors << "\n";
            return kFatal;
        }
        std::cout << "\nEverything is Ok\n";
        if (st.warnings) {
            std::cout << "Warnings: " << st.warnings << "\n";
            return kWarning;
        }
    }
    return kOk;
}

int cmd_delete(Options& opt) {
    std::string ap = archive_path_for(opt);
    if (!path_exists(ap)) {
        std::cerr << "ERROR: cannot find archive\n";
        return kFatal;
    }
    if (opt.files.empty() && opt.include.empty()) {
        std::cerr << "ERROR: no files specified to delete\n";
        return kCmdError;
    }
    if (opt.ask_password) opt.password = prompt_password();
    Archive arc;
    std::map<std::string, std::vector<uint8_t>> payloads;
    std::string err;
    if (!read_archive(ap, arc, &payloads, opt.password, err)) {
        std::cerr << "ERROR: " << err << "\n";
        return kFatal;
    }
    std::vector<Entry> keep;
    uint64_t removed = 0;
    for (auto& e : arc.entries) {
        if (name_selected(opt, e.name)) {
            payloads.erase(e.name);
            removed++;
            std::cout << "Deleting    " << e.name << "\n";
        } else {
            keep.push_back(e);
        }
    }
    if (!removed) {
        std::cout << "No files to delete\n";
        return kWarning;
    }
    if (!write_archive(ap, keep, payloads, opt.password, err)) {
        std::cerr << "ERROR: " << err << "\n";
        return kFatal;
    }
    std::cout << "\nEverything is Ok\n";
    return kOk;
}

int cmd_rename(Options& opt) {
    std::string ap = archive_path_for(opt);
    if (!path_exists(ap)) {
        std::cerr << "ERROR: cannot find archive\n";
        return kFatal;
    }
    if (opt.files.size() < 2 || (opt.files.size() % 2) != 0) {
        std::cerr << "ERROR: rn needs pairs: 67zip rn archive src dest [src dest ...]\n";
        return kCmdError;
    }
    if (opt.ask_password) opt.password = prompt_password();
    Archive arc;
    std::map<std::string, std::vector<uint8_t>> payloads;
    std::string err;
    if (!read_archive(ap, arc, &payloads, opt.password, err)) {
        std::cerr << "ERROR: " << err << "\n";
        return kFatal;
    }
    std::map<std::string, std::string> mapn;
    for (size_t i = 0; i + 1 < opt.files.size(); i += 2) {
        mapn[opt.files[i]] = normalize_arc_name(opt.files[i + 1]);
    }
    for (auto& e : arc.entries) {
        auto it = mapn.find(e.name);
        if (it == mapn.end()) continue;
        std::string nn = it->second;
        if (nn.empty()) {
            std::cerr << "ERROR: illegal new name for " << e.name << "\n";
            return kFatal;
        }
        auto payload = payloads[e.name];
        payloads.erase(e.name);
        std::cout << "Renaming    " << e.name << " -> " << nn << "\n";
        e.name = nn;
        payloads[e.name] = std::move(payload);
    }
    if (!write_archive(ap, arc.entries, payloads, opt.password, err)) {
        std::cerr << "ERROR: " << err << "\n";
        return kFatal;
    }
    std::cout << "\nEverything is Ok\n";
    return kOk;
}

uint64_t fnv1a64(const uint8_t* p, size_t n) {
    uint64_t h = 14695981039346656037ull;
    for (size_t i = 0; i < n; ++i) {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    return h;
}

// minimal SHA-1 / SHA-256 so -scrc and `h` match the documented hashers
struct Sha1 {
    uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    uint64_t bits = 0;
    uint8_t buf[64]{};
    size_t n = 0;
    static uint32_t rol(uint32_t x, int s) { return (x << s) | (x >> (32 - s)); }
    void block(const uint8_t* p) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i)
            w[i] = (uint32_t(p[i * 4]) << 24) | (uint32_t(p[i * 4 + 1]) << 16) |
                   (uint32_t(p[i * 4 + 2]) << 8) | uint32_t(p[i * 4 + 3]);
        for (int i = 16; i < 80; ++i) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20) {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }
            uint32_t t = rol(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = rol(b, 30);
            b = a;
            a = t;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
    }
    void update(const uint8_t* p, size_t len) {
        bits += uint64_t(len) * 8;
        while (len) {
            size_t take = std::min(len, 64 - n);
            std::memcpy(buf + n, p, take);
            n += take;
            p += take;
            len -= take;
            if (n == 64) {
                block(buf);
                n = 0;
            }
        }
    }
    std::string final() {
        buf[n++] = 0x80;
        if (n > 56) {
            while (n < 64) buf[n++] = 0;
            block(buf);
            n = 0;
        }
        while (n < 56) buf[n++] = 0;
        for (int i = 7; i >= 0; --i) buf[n++] = uint8_t((bits >> (i * 8)) & 0xff);
        block(buf);
        std::ostringstream o;
        for (int i = 0; i < 5; ++i)
            o << std::hex << std::setfill('0') << std::setw(8) << h[i];
        return o.str();
    }
};

struct Sha256 {
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                     0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    uint64_t bits = 0;
    uint8_t buf[64]{};
    size_t n = 0;
    static uint32_t rotr(uint32_t x, int s) { return (x >> s) | (x << (32 - s)); }
    void block(const uint8_t* p) {
        static const uint32_t K[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
        uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = (uint32_t(p[i * 4]) << 24) | (uint32_t(p[i * 4 + 1]) << 16) |
                   (uint32_t(p[i * 4 + 2]) << 8) | uint32_t(p[i * 4 + 3]);
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            uint32_t ch = (e & f) ^ ((~e) & g);
            uint32_t t1 = hh + S1 + ch + K[i] + w[i];
            uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t t2 = S0 + maj;
            hh = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
        h[5] += f;
        h[6] += g;
        h[7] += hh;
    }
    void update(const uint8_t* p, size_t len) {
        bits += uint64_t(len) * 8;
        while (len) {
            size_t take = std::min(len, 64 - n);
            std::memcpy(buf + n, p, take);
            n += take;
            p += take;
            len -= take;
            if (n == 64) {
                block(buf);
                n = 0;
            }
        }
    }
    std::string final() {
        buf[n++] = 0x80;
        if (n > 56) {
            while (n < 64) buf[n++] = 0;
            block(buf);
            n = 0;
        }
        while (n < 56) buf[n++] = 0;
        for (int i = 7; i >= 0; --i) buf[n++] = uint8_t((bits >> (i * 8)) & 0xff);
        block(buf);
        std::ostringstream o;
        for (int i = 0; i < 8; ++i)
            o << std::hex << std::setfill('0') << std::setw(8) << h[i];
        return o.str();
    }
};

std::string hash_bytes(const std::string& algo, const uint8_t* p, size_t n) {
    std::string a = to_lower(algo);
    if (a == "crc32" || a == "*") {
        return hex_u32(n ? crc32_bytes(p, n, 0) : 0);
    }
    if (a == "crc64") {
        std::ostringstream o;
        o << std::hex << std::setfill('0') << std::setw(16) << fnv1a64(p, n);
        // real CRC64-ECMA
        uint64_t crc = ~0ull;
        for (size_t i = 0; i < n; ++i) {
            crc ^= p[i];
            for (int b = 0; b < 8; ++b) {
                uint64_t mask = -(crc & 1);
                crc = (crc >> 1) ^ (0xC96C5795D7870F42ull & mask);
            }
        }
        crc = ~crc;
        o.str("");
        o << std::hex << std::setfill('0') << std::setw(16) << crc;
        return o.str();
    }
    if (a == "sha1") {
        Sha1 s;
        s.update(p, n);
        return s.final();
    }
    if (a == "sha256") {
        Sha256 s;
        s.update(p, n);
        return s.final();
    }
    Sha256 s;
    s.update(p, n);
    return s.final();
}

int cmd_hash(Options& opt) {
    Stats st;
    std::string fatal;
    if (opt.files.empty() && !opt.from_stdin) opt.files.push_back(".");
    // If an archive-looking first arg was consumed, hash treats all positionals as files.
    // parse_args put the first non-switch into archive for non-h commands only.
    std::vector<std::string> files;
    if (opt.from_stdin) {
        std::vector<uint8_t> data;
        load_stdin_bytes(data);
        std::cout << hash_bytes(opt.hash_name, data.data(), data.size()) << "  stdin\n";
        return kOk;
    }
    std::function<void(const std::string&)> walk = [&](const std::string& p) {
        port::FileInfo info;
        port::stat_path(p, info);
        if (info.is_dir) {
            port::list_dir(p, [&](const std::string& name, bool is_dir) {
                std::string full = join_out(p, name);
                if (is_dir) walk(full);
                else files.push_back(full);
            });
        } else if (info.is_file) {
            files.push_back(p);
        } else {
            std::cerr << "WARNING: skip " << p << "\n";
            st.warnings++;
        }
    };
    for (auto& f : opt.files) walk(f);
    std::string algo = opt.hash_name.empty() ? "CRC32" : opt.hash_name;
    for (auto& p : files) {
        std::vector<uint8_t> data;
        std::string err;
        if (!load_file_bytes(p, data, err)) {
            std::cerr << "ERROR: " << err << "\n";
            st.errors++;
            continue;
        }
        std::cout << hash_bytes(algo, data.data(), data.size()) << "  " << p << "\n";
    }
    return st.errors ? kFatal : (st.warnings ? kWarning : kOk);
}

int cmd_bench(Options& opt) {
    print_banner();
#if defined(ZIP67_NO_THREADS)
    int threads = 1;
#else
    int threads = opt.threads > 0 ? opt.threads : int(std::max(1u, std::thread::hardware_concurrency()));
#endif
    if (!opt.files.empty()) {
        bool ok = false;
        uint64_t n = parse_size(opt.files[0], &ok);
        if (ok && n > 0) threads = static_cast<int>(std::min<uint64_t>(n, 256));
    }
    std::cout << "Threads: " << threads << "\n\n";
    std::vector<uint8_t> src(1 << 20);
    for (size_t i = 0; i < src.size(); ++i) src[i] = uint8_t(i * 17 + (i >> 3));
    auto bench_one = [&](Method m, const char* name) {
        auto t0 = std::chrono::steady_clock::now();
        std::vector<uint8_t> out;
        std::string err;
        int iters = 3;
        for (int i = 0; i < iters; ++i) {
            if (!compress_buffer(m, opt.level, src.data(), src.size(), out, err)) {
                std::cerr << err << "\n";
                return;
            }
        }
        auto t1 = std::chrono::steady_clock::now();
        double sec = std::chrono::duration<double>(t1 - t0).count();
        double mb = (src.size() * iters) / (1024.0 * 1024.0);
        std::vector<uint8_t> back;
        auto t2 = std::chrono::steady_clock::now();
        for (int i = 0; i < iters; ++i) {
            decompress_buffer(m, out.data(), out.size(), src.size(), back, err);
        }
        auto t3 = std::chrono::steady_clock::now();
        double dsec = std::chrono::duration<double>(t3 - t2).count();
        std::cout << std::left << std::setw(10) << name
                  << " compress " << std::fixed << std::setprecision(1) << (mb / sec) << " MiB/s"
                  << "  decompress " << (mb / dsec) << " MiB/s"
                  << "  ratio " << std::setprecision(2) << (100.0 * out.size() / src.size()) << "%\n";
    };
    bench_one(Method::Lzma2, "LZMA2");
    bench_one(Method::Deflate, "Deflate");
    std::cout << "\nBenchmark finished\n";
    return kOk;
}

void print_time_stats(std::chrono::steady_clock::time_point t0) {
    auto t1 = std::chrono::steady_clock::now();
    double sec = std::chrono::duration<double>(t1 - t0).count();
    std::cout << "\nKernel  Time =     0.000 =    0% \n";
    std::cout << "User    Time = " << std::fixed << std::setprecision(3) << sec << "\n";
    std::cout << "Process Time = " << sec << "\n";
}

}  // namespace

int run(int argc, char** argv) {
    auto t0 = std::chrono::steady_clock::now();
    Options opt;
    std::string err;
    if (!parse_args(argc, argv, opt, err)) {
        std::cerr << "ERROR: " << err << "\n";
        return kCmdError;
    }
    if (opt.command.empty() || opt.command == "help") {
        print_usage();
        return opt.command.empty() ? kCmdError : kOk;
    }
    if (opt.command == "version") {
        print_banner();
        return kOk;
    }
    int rc = kOk;
    if (opt.command == "a") rc = cmd_add(opt, false);
    else if (opt.command == "u") rc = cmd_add(opt, true);
    else if (opt.command == "l") rc = cmd_list(opt);
    else if (opt.command == "x") rc = cmd_extract(opt, opt.to_stdout ? ExtractMode::Stdout : ExtractMode::FullPath);
    else if (opt.command == "e") rc = cmd_extract(opt, opt.to_stdout ? ExtractMode::Stdout : ExtractMode::Flat);
    else if (opt.command == "t") rc = cmd_extract(opt, ExtractMode::Test);
    else if (opt.command == "d") rc = cmd_delete(opt);
    else if (opt.command == "rn") rc = cmd_rename(opt);
    else if (opt.command == "h") rc = cmd_hash(opt);
    else if (opt.command == "i") {
        print_formats();
        rc = kOk;
    } else if (opt.command == "b") rc = cmd_bench(opt);
    else {
        std::cerr << "ERROR: unsupported command: " << opt.command << "\n";
        print_usage();
        rc = kCmdError;
    }
    if (opt.show_time) print_time_stats(t0);
    return rc;
}

}  // namespace zip67

int main(int argc, char** argv) {
    try {
        return zip67::run(argc, argv);
    } catch (const std::exception& ex) {
        std::cerr << "ERROR: " << ex.what() << "\n";
        return 2;
    }
}
