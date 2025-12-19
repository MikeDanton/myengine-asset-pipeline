// assetc - simple asset compiler
// Scans an input directory recursively, computes content hashes, compares
// against a small incremental cache, and writes a .pak using the writer API.

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <unordered_map>
#include <filesystem>
#include <optional>
#include <sstream>
#include <iomanip>

#include "pak_writer.h"
#include "hash128.h"

namespace fs = std::filesystem;
using namespace asset;

static std::string to_hex(const pak::Hash128 &h) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (size_t i = 0; i < sizeof(h.bytes); ++i) {
        oss << std::setw(2) << static_cast<int>(h.bytes[i]);
    }
    return oss.str();
}

static std::optional<pak::Hash128> compute_hash_for_buffer(const std::vector<uint8_t> &buf) {
    if (buf.empty()) return pak::ComputeHash128(nullptr, 0);
    return pak::ComputeHash128(buf.data(), buf.size());
}

// Simple cache format: each line `key<TAB>hexhash`.
static std::unordered_map<std::string, std::string> load_cache(const fs::path &p) {
    std::unordered_map<std::string, std::string> out;
    std::ifstream is(p);
    if (!is) return out;
    std::string line;
    while (std::getline(is, line)) {
        if (line.empty()) continue;
        auto tab = line.find('\t');
        if (tab == std::string::npos) continue;
        std::string key = line.substr(0, tab);
        std::string hex = line.substr(tab+1);
        out.emplace(std::move(key), std::move(hex));
    }
    return out;
}

static bool write_cache(const fs::path &p, const std::unordered_map<std::string, std::string> &m) {
    std::ofstream os(p, std::ios::trunc);
    if (!os) return false;
    for (const auto &kv : m) {
        os << kv.first << '\t' << kv.second << '\n';
    }
    return !!os;
}

static bool read_file_bytes(const fs::path &p, std::vector<uint8_t> &out) {
    std::ifstream is(p, std::ios::binary | std::ios::in);
    if (!is) return false;
    is.seekg(0, std::ios::end);
    std::streamsize sz = is.tellg();
    is.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(std::max<std::streamsize>(0, sz)));
    if (sz > 0) is.read(reinterpret_cast<char*>(out.data()), sz);
    return !!is || sz == 0;
}

static std::string canonical_key_from_path(const fs::path &root, const fs::path &file) {
    auto rel = fs::relative(file, root);
    // Use generic_string to get forward slashes for portability
    return rel.generic_string();
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cout << "Usage: assetc <input_dir> <output.pak> [cachefile]\n";
        return 1;
    }
    fs::path input = argv[1];
    fs::path outPak = argv[2];
    fs::path cacheFile = (argc >= 4) ? fs::path(argv[3]) : fs::path(outPak.string() + ".cache");

    if (!fs::exists(input) || !fs::is_directory(input)) {
        std::cerr << "input directory does not exist: " << input << "\n";
        return 2;
    }

    auto oldCache = load_cache(cacheFile);
    std::unordered_map<std::string, std::string> newCache;

    struct ScanItem { fs::path path; std::string key; std::vector<uint8_t> bytes; std::string hexhash; };
    std::vector<ScanItem> items;

    // Walk and hash
    for (auto &it : fs::recursive_directory_iterator(input)) {
        if (!it.is_regular_file()) continue;
        fs::path p = it.path();
        std::string key = canonical_key_from_path(input, p);
        std::vector<uint8_t> buf;
        if (!read_file_bytes(p, buf)) {
            std::cerr << "Failed to read file: " << p << "\n";
            return 3;
        }
        pak::Hash128 h = pak::ComputeHash128(buf.empty() ? nullptr : buf.data(), buf.size());
        std::string hex = to_hex(h);
        items.push_back(ScanItem{p, key, std::move(buf), hex});
        newCache.emplace(key, hex);
    }

    // Quick compare: if the new cache matches the old cache and the output exists, skip writing.
    if (!newCache.empty() && newCache == oldCache && fs::exists(outPak)) {
        std::cout << "No changes detected; skipping pak write.\n";
        return 0;
    }

    // Build writer entries from scanned items
    std::vector<PakWriterEntry> entries;
    entries.reserve(items.size());
    for (auto &si : items) {
        PakWriterEntry e;
        e.canonical_key = si.key;
        e.type = pak::AssetType::RawBytes;
        e.compression = pak::Compression::None;
        e.bytes = std::move(si.bytes);
        entries.push_back(std::move(e));
    }

    std::cout << "Writing pak: " << outPak << " (" << entries.size() << " entries)\n";
    auto res = WritePak(outPak.string(), entries);
    if (!res) {
        std::cerr << "WritePak failed: " << static_cast<int>(res.error) << " " << res.message << "\n";
        return 4;
    }

    // Persist cache
    if (!write_cache(cacheFile, newCache)) {
        std::cerr << "Warning: failed to write cache file: " << cacheFile << "\n";
    }

    std::cout << "Pak written successfully." << std::endl;
    return 0;
}

