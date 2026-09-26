// Standalone checks for the portable wildcard and mode-bit layer.
// Build: c++ -std=c++17 -Wall -Wextra -o port_test src/port_test.cpp src/port.cpp
#include "port.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

static int g_fail = 0;

static void expect(bool cond, const char* what) {
    if (cond) return;
    std::cerr << "FAIL: " << what << "\n";
    ++g_fail;
}

int main() {
    using zip67::port::wild_match;
    expect(wild_match("*.txt", "a.txt"), "star suffix");
    expect(!wild_match("*.txt", "a.txt.bak"), "star suffix negative");
    expect(!wild_match("*.txt", "dir/a.txt"), "star does not cross slash");
    expect(wild_match("dir/*.txt", "dir/a.txt"), "dir star");
    expect(wild_match("file?", "file1"), "question");
    expect(!wild_match("file?", "file12"), "question length");
    expect(!wild_match("file?", "file/"), "question is not slash");
    expect(wild_match("a[bc].txt", "ab.txt"), "class");
    expect(!wild_match("a[bc].txt", "ad.txt"), "class negative");
    expect(wild_match("a[!b].txt", "ac.txt"), "negated class");
    expect(!wild_match("a[!b].txt", "ab.txt"), "negated class hit");
    expect(wild_match("*", "abc"), "star all");
    expect(wild_match("*", ""), "star empty");
    expect(!wild_match("*", "a/b"), "star not slash");
    expect(wild_match("a\\*b", "a*b"), "escaped star");
    expect(std::string(zip67::port::os_name()).size() > 0, "os name");
    expect(zip67::port::os_ready(), "this host is in the ready set");
    expect(std::string(zip67::port::arch_name()).size() > 0, "arch name");
    expect(zip67::port::native_sep() == '/' || zip67::port::native_sep() == '\\', "sep");
    auto ts = zip67::port::format_local_time(0);
    expect(ts.size() == 19, "zero time width");
    auto ts2 = zip67::port::format_local_time(1'700'000'000);
    expect(ts2.size() == 19 && ts2[4] == '-', "real time");
    std::cout << "os=" << zip67::port::os_name() << " arch=" << zip67::port::arch_name() << "\n";
    if (g_fail) {
        std::cerr << g_fail << " failure(s)\n";
        return 1;
    }
    std::cout << "port_test ok\n";
    return 0;
}
