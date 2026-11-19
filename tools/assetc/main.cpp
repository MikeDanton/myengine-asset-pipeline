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
#include <cctype>
#include <chrono>
#include <array>
#include <blake3.h>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

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

// Hash output packs incrementally; a cache hit must not allocate another pack-sized buffer.
static auto hash_file(const fs::path& path) -> std::optional<std::string> {
    std::ifstream input(path, std::ios::binary);
    if (!input) return std::nullopt;
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    std::array<char, 65536> chunk{};
    while (input) {
        input.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        const auto count = input.gcount();
        if (count > 0) blake3_hasher_update(&hasher, chunk.data(), static_cast<std::size_t>(count));
    }
    if (!input.eof()) return std::nullopt;
    pak::Hash128 digest;
    blake3_hasher_finalize(&hasher, digest.bytes, sizeof(digest.bytes));
    return to_hex(digest);
}

static void replace_output(const fs::path& candidate, const fs::path& destination) {
#ifdef _WIN32
    if (!MoveFileExW(candidate.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "replace output pack");
    }
#else
    fs::rename(candidate, destination);
#endif
}

// Versioned policy/output digest header, then `key<TAB>hexhash` records.
static auto load_cache(const fs::path &path, const std::string& policy, const std::string& outputHash)
    -> std::optional<std::unordered_map<std::string, std::string>> {
    std::unordered_map<std::string, std::string> out;
    std::ifstream in(path);
    if (!in) return std::nullopt;
    std::string line;
    if (!std::getline(in, line) || line != policy ||
        !std::getline(in, line) || line != outputHash) return std::nullopt;
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

static auto write_cache(const fs::path &path, const std::unordered_map<std::string, std::string> &map,
                        const std::string& policy, const std::string& outputHash) -> bool {
    std::ofstream out(path, std::ios::trunc);
    if (!out) { return false; }
    out << policy << '\n' << outputHash << '\n';
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
    if (size < 0) return false;
    if (size == 0) {
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
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
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

int main(int argc, char** argv) try {
    bool useCompression = false;
    std::vector<std::string> args;
    if (argc > 0) {
        args.reserve(static_cast<std::size_t>(argc));
    }

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--compress" || arg == "-z") {
            useCompression = true;
        } else if (arg.starts_with('-')) {
            std::cerr << "Unknown option: " << arg << '\n';
            return 1;
        } else {
            args.push_back(arg);
        }
    }

    if (args.size() < 2 || args.size() > 3) {
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

    input = fs::weakly_canonical(input);
    outPak = fs::weakly_canonical(outPak);
    cacheFile = fs::weakly_canonical(cacheFile);
    if (outPak == cacheFile) {
        std::cerr << "Output pack and cache must be different files\n";
        return 2;
    }
    const std::string policy = useCompression ? "assetc-cache-v2 compress=1" : "assetc-cache-v2 compress=0";
    const std::string oldOutputHash = hash_file(outPak).value_or(std::string{});
    auto oldCache = load_cache(cacheFile, policy, oldOutputHash);
    std::unordered_map<std::string, std::string> newCache;

    struct ScanItem { fs::path path; std::string key; std::vector<uint8_t> bytes; std::string hexhash; };
    std::vector<ScanItem> items;

    // Walk and hash
    for (const auto &dirEntry : fs::recursive_directory_iterator(input)) {
        if (!dirEntry.is_regular_file()) { continue; }
        const auto &filePath = dirEntry.path();
        // Fail closed if candidate generation outputs leaked into this tree.
        // Check before the pack cache shortcut, including nested directories.
        if (filePath.filename() == ".asset-pipeline-quarantine.json") {
            std::cerr << "Refusing to package unreviewed generation candidates: "
                      << filePath << "\n";
            return 3;
        }
        const auto resolved = fs::weakly_canonical(filePath);
        if (resolved == outPak || resolved == cacheFile) continue;
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
    if (oldCache && newCache == *oldCache && !oldOutputHash.empty()) {
        std::cout << "No changes detected; skipping pak write.\n";
        return 0;
    }

    std::sort(items.begin(), items.end(), [](const ScanItem& a, const ScanItem& b) {
        return a.key < b.key;
    });

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
    // Write privately on the destination filesystem. A failed compression/write
    // must not truncate a previously usable output pack.
    fs::path temporary;
    for (unsigned int attempt = 0; attempt < 100; ++attempt) {
        temporary = outPak.string() + ".assetc-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
            "-" + std::to_string(attempt);
        if (fs::create_directory(temporary)) break;
        temporary.clear();
    }
    if (temporary.empty()) { std::cerr << "Cannot reserve temporary output\n"; return 4; }
    struct Cleanup {
        fs::path path;
        ~Cleanup() { std::error_code ignored; fs::remove_all(path, ignored); }
    } cleanup{temporary};
    const auto candidate = temporary / "pack";
    auto res = WritePak(candidate.string(), entries);
    if (!res) {
        std::cerr << "WritePak failed: " << static_cast<int>(res.error) << " " << res.message << "\n";
        return 4;
    }

    replace_output(candidate, outPak);
    const auto outputHash = hash_file(outPak);
    if (!outputHash) return 4;

    // Persist cache
    if (!write_cache(cacheFile, newCache, policy, *outputHash)) {
        std::cerr << "Warning: failed to write cache file: " << cacheFile << "\n";
    }

    std::cout << "Pak written successfully." << '\n';
    return 0;
} catch (const std::exception& error) {
    std::cerr << "assetc: " << error.what() << '\n';
    return 4;
}
