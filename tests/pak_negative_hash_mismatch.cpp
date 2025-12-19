// pak_negative_hash_mismatch.cpp
// Writes a pak then corrupts stored data for an entry to trigger HashMismatch on Load.

#include <iostream>
#include <vector>
#include <string>
#include <fstream>

#include "pak_writer.h"
#include "asset_runtime.h"
#include "pak_format.h"
#include "xxhash64.h"
#include "io_utils.h"

auto main() -> int {
    using namespace asset;

    std::vector<PakWriterEntry> entries;
    PakWriterEntry a;
    a.canonical_key = "hm/one";
    a.type = pak::AssetType::RawBytes;
    std::string payload = "original-data";
    a.bytes.assign(payload.begin(), payload.end());
    entries.push_back(a);

    const std::string outPath = "test_neg_hash.pak";
    auto res = WritePak(outPath, entries);
    if (!res) { std::cerr << "WritePak failed: " << static_cast<int>(res.error) << " " << res.message << '\n'; return 2; }

    // Locate the data_offset for the entry by reading the header and TOC.
    pak::PakHeaderWire hdr{};
    pak::PakTocEntryWire te{};
    bool found = false;
    {
        std::fstream f(outPath, std::ios::binary | std::ios::in | std::ios::out);
        if (!f) { std::cerr << "Open failed" << '\n'; return 3; }
        if (!asset::util::read_struct(f, hdr)) { std::cerr << "Read hdr failed" << '\n'; return 4; }
        f.seekg(static_cast<std::streamoff>(hdr.toc_offset), std::ios::beg);
        for (std::uint32_t i = 0; i < hdr.toc_count; ++i) {
            pak::PakTocEntryWire local{};
            if (!asset::util::read_struct(f, local)) { std::cerr << "Read toc entry failed" << '\n'; return 5; }
            if (local.id == static_cast<pak::AssetId>(XxHash64(a.canonical_key))) {
                te = local;
                found = true;
                break;
            }
        }
    }
    if (!found) { std::cerr << "Entry not found in TOC" << '\n'; return 6; }

    // Corrupt first byte of stored blob
    {
        std::fstream f(outPath, std::ios::binary | std::ios::in | std::ios::out);
        if (!f) { std::cerr << "Open2 failed" << '\n'; return 7; }
        if (te.stored_size == 0) { std::cerr << "No stored bytes" << '\n'; return 8; }
        f.seekp(static_cast<std::streamoff>(te.data_offset), std::ios::beg);
        unsigned char c = 0xFF;
        // write single byte without reinterpret_cast
        char tmp = static_cast<char>(c);
        f.write(&tmp, 1);
    }

    // Now open mount and attempt to load; expecting HashMismatch
    AssetError err = AssetError::Ok;
    auto mount = PakMount::Open(outPath, err, true);
    if (!mount) { std::cerr << "Open failed: " << static_cast<int>(err) << '\n'; return 9; }
    auto& mountRef = *mount;

    AssetError loadErr = AssetError::Ok;
    auto blob = mountRef.Load(static_cast<AssetId>(XxHash64(a.canonical_key)), loadErr, true);
    if (blob) { std::cerr << "Expected HashMismatch but Load returned data" << '\n'; return 10; }
    if (loadErr != AssetError::HashMismatch) { std::cerr << "Unexpected load error: " << static_cast<int>(loadErr) << '\n'; return 11; }

    std::cout << "HashMismatch test passed." << '\n';
    return 0;
}
