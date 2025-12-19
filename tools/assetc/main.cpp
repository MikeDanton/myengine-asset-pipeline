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
#include <cstring>
#include <algorithm>

#include "pak_writer.h"
#include "hash128.h"

namespace fs = std::filesystem;
using namespace asset;

static auto to_hex(const pak::Hash128 &hash) -> std::string {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (unsigned char byte : hash.bytes) {
        oss << std::setw(2) << static_cast<int>(byte);
    }
    return oss.str();
}

// Simple cache format: each line `key<TAB>hexhash`.
static auto load_cache(const fs::path &path) -> std::unordered_map<std::string, std::string> {
    std::unordered_map<std::string, std::string> out;
    std::ifstream in(path);
    if (!in) { return out; }
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) { continue; }
        auto tab = line.find('\t');
        if (tab == std::string::npos) { continue; }
        std::string key = line.substr(0, tab);
        std::string hex = line.substr(tab+1);
        out.emplace(std::move(key), std::move(hex));
    }
    return out;
}

static auto write_cache(const fs::path &path, const std::unordered_map<std::string, std::string> &map) -> bool {
    std::ofstream out(path, std::ios::trunc);
    if (!out) { return false; }
    for (const auto &kvp : map) {
        out << kvp.first << '\t' << kvp.second << '\n';
    }
    return !!out;
}

static auto read_file_bytes(const fs::path &path, std::vector<uint8_t> &out) -> bool {
    std::ifstream in(path, std::ios::binary | std::ios::in);
    if (!in) { return false; }
    in.seekg(0, std::ios::end);
    std::streamsize size = in.tellg();
    in.seekg(0, std::ios::beg);
    if (size <= 0) {
        out.clear();
        return !!in;
    }
    out.resize(static_cast<size_t>(size));
    in.read(reinterpret_cast<char*>(out.data()), size);
    return !!in;
}

static auto canonical_key_from_path(const fs::path &root, const fs::path &file) -> std::string {
    auto rel = fs::relative(file, root);
    // Use generic_string to get forward slashes for portability
    return rel.generic_string();
}

static auto detect_asset_type(const fs::path &path) -> pak::AssetType {
    auto ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp") {
        return pak::AssetType::Texture2D;
    }
    if (ext == ".spv") {
        return pak::AssetType::ShaderSpirv;
    }
    if (ext == ".json") {
        return pak::AssetType::Json;
    }
    if (ext == ".yaml" || ext == ".yml") {
        return pak::AssetType::Yaml;
    }
    return pak::AssetType::RawBytes;
}

int main(int argc, char** argv) {
    bool useCompression = false;
    std::vector<std::string> args;
    if (argc > 0) {
        args.reserve(static_cast<std::size_t>(argc));
    }

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--compress" || arg == "-z") {
            useCompression = true;
        } else {
            args.push_back(arg);
        }
    }

    if (args.size() < 2) {
        std::cout << "Usage: assetc <input_dir> <output.pak> [cachefile] [--compress]\n";
        return 1;
    }

    fs::path input(args[0]);
    fs::path outPak(args[1]);
    fs::path cacheFile = (args.size() >= 3) ? fs::path(args[2]) : fs::path(outPak.string() + ".cache");

    if (!fs::exists(input) || !fs::is_directory(input)) {
        std::cerr << "input directory does not exist: " << input << "\n";
        return 2;
    }

    auto oldCache = load_cache(cacheFile);
    std::unordered_map<std::string, std::string> newCache;

    struct ScanItem { fs::path path; std::string key; std::vector<uint8_t> bytes; std::string hexhash; };
    std::vector<ScanItem> items;

    // Walk and hash
    for (const auto &dirEntry : fs::recursive_directory_iterator(input)) {
        if (!dirEntry.is_regular_file()) { continue; }
        const auto &filePath = dirEntry.path();
        std::string key = canonical_key_from_path(input, filePath);
        std::vector<uint8_t> buf;
        if (!read_file_bytes(filePath, buf)) {
            std::cerr << "Failed to read file: " << filePath << "\n";
            return 3;
        }
        pak::Hash128 hash = pak::ComputeHash128(buf.empty() ? nullptr : buf.data(), buf.size());
        std::string hex = to_hex(hash);
        ScanItem scanItem;
        scanItem.path = filePath;
        scanItem.key = key;
        scanItem.bytes = std::move(buf);
        scanItem.hexhash = hex;
        items.push_back(std::move(scanItem));
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
    for (auto &scanItem : items) {
        PakWriterEntry entry;
        entry.canonical_key = scanItem.key;
        entry.type = detect_asset_type(scanItem.path);
        entry.compression = useCompression ? pak::Compression::Zstd : pak::Compression::None;
        entry.bytes = std::move(scanItem.bytes);
        entries.push_back(std::move(entry));
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

    std::cout << "Pak written successfully." << '\n';
    return 0;
}

