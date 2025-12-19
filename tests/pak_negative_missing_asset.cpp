// pak_negative_missing_asset.cpp
// Writes a pak and queries for a missing asset id to ensure NotFound is returned.

#include <iostream>
#include <vector>
#include <string>

#include "pak_writer.h"
#include "asset_runtime.h"
#include "xxhash64.h"

auto main() -> int {
    using namespace asset;

    std::vector<PakWriterEntry> entries;
    PakWriterEntry entry;
    entry.canonical_key = "exist/one";
    entry.type = pak::AssetType::RawBytes;
    entry.bytes = { '1', '2', '3' };
    entries.push_back(entry);

    const std::string outPath = "test_neg_missing.pak";
    auto res = WritePak(outPath, entries);
    if (!res) { std::cerr << "WritePak failed: " << static_cast<int>(res.error) << " " << res.message << '\n'; return 2; }

    AssetError err = AssetError::Ok;
    auto mount = PakMount::Open(outPath, err, true);
    if (!mount) { std::cerr << "Open failed: " << static_cast<int>(err) << '\n'; return 3; }
    auto& mountRef = *mount;

    auto missingId = static_cast<AssetId>(XxHash64(std::string("no/such/asset")));
    if (mountRef.Has(missingId)) { std::cerr << "Has() unexpectedly found missing asset" << '\n'; return 4; }

    auto info = mountRef.GetInfo(missingId);
    if (info) { std::cerr << "GetInfo unexpectedly returned info for missing asset" << '\n'; return 5; }

    AssetError loadErr = AssetError::Ok;
    auto blob = mountRef.Load(missingId, loadErr, true);
    if (blob) { std::cerr << "Load unexpectedly returned blob for missing asset" << '\n'; return 6; }
    if (loadErr != AssetError::NotFound) { std::cerr << "Expected NotFound error" << '\n'; return 7; }

    std::cout << "Missing asset test passed." << '\n';
    return 0;
}
