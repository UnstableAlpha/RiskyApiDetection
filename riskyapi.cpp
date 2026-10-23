// riskyapi - recursively scan C/C++ source for risky Win32 API calls.
// Build (MSVC):  cl /std:c++17 /EHsc riskyapi.cpp
// Build (MinGW): g++ -std=c++17 riskyapi.cpp -o riskyapi.exe
// Usage: riskyapi <root-dir> [--csv]   (root defaults to ".")
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

static const char* APIS[] = {
    "CreateRemoteThread", "WriteProcessMemory", "VirtualAllocEx",
    "QueueUserAPC", "NtUnmapViewOfSection", "IsDebuggerPresent",
    "CheckRemoteDebuggerPresent", "NtQueryInformationProcess", "CreateService",
};
static const int N_APIS = (int)(sizeof(APIS) / sizeof(APIS[0]));

static bool is_ident(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '_';
}

struct Hit { int col; const char* api; };

// Whole-word hits in one line. A trailing 'A'/'W' is allowed so the Win32
// ANSI/Unicode variants (e.g. CreateServiceA) still match. Reported name is
// the base name from APIS[].
static std::vector<Hit> find_hits(const std::string& line) {
    std::vector<Hit> hits;
    for (int a = 0; a < N_APIS; ++a) {
        const std::string api = APIS[a];
        for (size_t pos = 0; (pos = line.find(api, pos)) != std::string::npos;) {
            bool left_ok = (pos == 0) || !is_ident(line[pos - 1]);
            size_t end = pos + api.size();
            bool right_ok = (end >= line.size()) || !is_ident(line[end]);
            if (!right_ok) {
                char c = line[end];  // accept a terminal A/W (ANSI/Unicode)
                if ((c == 'A' || c == 'W') &&
                    (end + 1 >= line.size() || !is_ident(line[end + 1])))
                    right_ok = true;
            }
            if (left_ok && right_ok) hits.push_back({(int)pos + 1, APIS[a]});
            pos = end;
        }
    }
    std::sort(hits.begin(), hits.end(),
              [](const Hit& x, const Hit& y) { return x.col < y.col; });
    return hits;
}

static bool wanted_ext(const fs::path& p) {
    std::string e = p.extension().string();
    for (char& c : e) c = (char)std::tolower((unsigned char)c);
    return e == ".c" || e == ".cpp" || e == ".cc" || e == ".cxx" ||
           e == ".h" || e == ".hpp" || e == ".hxx" || e == ".hh";
}

static std::string csv_field(const std::string& s) {
    std::string o = "\"";
    for (char c : s) { if (c == '"') o += "\"\""; else o += c; }
    return o + "\"";
}

static std::string json_str(const std::string& s) {
    std::string o = "\"";
    for (unsigned char c : s) {
        switch (c) {
            case '"':  o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\b': o += "\\b";  break;
            case '\f': o += "\\f";  break;
            case '\n': o += "\\n";  break;
            case '\r': o += "\\r";  break;
            case '\t': o += "\\t";  break;
            default:
                if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); o += b; }
                else o += (char)c;
        }
    }
    return o + "\"";
}

enum class Mode { Report, Csv, Json };

// Scan one file. Returns hit count; updates output as it goes.
// json_first tracks whether the next JSON element is the first (comma control).
static long scan_file(const fs::path& path, Mode mode, bool& json_first) {
    std::ifstream in(path, std::ios::binary);
    if (!in) { std::fprintf(stderr, "warning: cannot open %s\n", path.string().c_str()); return 0; }
    std::string file = path.string();
    long count = 0;
    bool header_done = false;
    std::string line;
    int lineno = 0;
    while (std::getline(in, line)) {
        ++lineno;
        if (!line.empty() && line.back() == '\r') line.pop_back();  // CRLF
        if (lineno == 1 && line.size() >= 3 &&                      // UTF-8 BOM
            (unsigned char)line[0] == 0xEF && (unsigned char)line[1] == 0xBB &&
            (unsigned char)line[2] == 0xBF)
            line.erase(0, 3);
        for (const Hit& h : find_hits(line)) {
            ++count;
            if (mode == Mode::Csv) {
                std::printf("%s,%d,%d,%s,%s\n", csv_field(file).c_str(), lineno,
                            h.col, h.api, csv_field(line).c_str());
            } else if (mode == Mode::Json) {
                std::printf("%s{\"file\":%s,\"line\":%d,\"col\":%d,\"api\":\"%s\",\"linetext\":%s}",
                            json_first ? "\n  " : ",\n  ", json_str(file).c_str(),
                            lineno, h.col, h.api, json_str(line).c_str());
                json_first = false;
            } else {
                if (!header_done) { std::printf("%s\n", file.c_str()); header_done = true; }
                std::printf("  L%-5d C%-4d %-26s | %s\n", lineno, h.col, h.api, line.c_str());
            }
        }
    }
    if (mode == Mode::Report && header_done) std::printf("\n");
    return count;
}

static int run_selftest() {
    assert(find_hits("  x = VirtualAllocEx(h);").size() == 1);
    assert(find_hits("MyVirtualAllocEx(x)").empty());       // leading ident
    assert(find_hits("VirtualAllocExtra()").empty());        // trailing ident
    assert(find_hits("VirtualAllocEx").size() == 1);         // whole line
    auto c = find_hits("CreateServiceA(h);");                // ANSI variant
    assert(c.size() == 1 && std::string(c[0].api) == "CreateService");
    assert(find_hits("CreateServiceApiHelper()").empty());   // not a variant
    assert(find_hits("WriteProcessMemory(); VirtualAllocEx();").size() == 2);
    auto col = find_hits("IsDebuggerPresent()");
    assert(col.size() == 1 && col[0].col == 1);              // 1-based column
    std::printf("selftest OK\n");
    return 0;
}

int main(int argc, char** argv) {
    std::string root = ".";
    Mode mode = Mode::Report;
    bool selftest = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--csv") mode = Mode::Csv;
        else if (a == "--json") mode = Mode::Json;
        else if (a == "--selftest") selftest = true;
        else if (!a.empty() && a[0] == '-') { std::fprintf(stderr, "unknown option: %s\n", a.c_str()); return 2; }
        else root = a;
    }
    if (selftest) return run_selftest();

    std::error_code ec;
    if (!fs::exists(root, ec)) { std::fprintf(stderr, "error: path not found: %s\n", root.c_str()); return 2; }

    if (mode == Mode::Csv) std::printf("file,line,col,api,linetext\n");
    if (mode == Mode::Json) std::printf("[");

    long files_hit = 0, total = 0;
    bool json_first = true;
    auto opts = fs::directory_options::skip_permission_denied;
    for (fs::recursive_directory_iterator it(root, opts, ec), end; it != end; it.increment(ec)) {
        if (ec) { std::fprintf(stderr, "warning: %s\n", ec.message().c_str()); ec.clear(); continue; }
        std::error_code fec;
        if (!it->is_regular_file(fec) || !wanted_ext(it->path())) continue;
        long n = scan_file(it->path(), mode, json_first);
        if (n > 0) { ++files_hit; total += n; }
    }

    if (mode == Mode::Report) std::printf("Summary: %ld file(s), %ld hit(s).\n", files_hit, total);
    if (mode == Mode::Json) std::printf(json_first ? "]\n" : "\n]\n");
    return total > 0 ? 1 : 0;
}
