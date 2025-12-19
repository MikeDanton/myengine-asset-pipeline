// pak_negative_missing_asset.cpp
// Writes a pak and queries for a missing asset id to ensure NotFound is returned.

#include <iostream>
#include <vector>
#include <string>

#include "pak_writer.h"
#include "asset_runtime.h"
#include "xxhash64.h"

int main() {
    using namespace asset;

    std::vector<PakWriterEntry> entries;
    PakWriterEntry a;
    a.canonical_key = "exist/one";
    a.type = pak::AssetType::RawBytes;
    a.bytes = { '1', '2', '3' };
    entries.push_back(a);

    const std::string outPath = "test_neg_missing.pak";
    auto res = WritePak(outPath, entries);
    if (!res) { std::cerr << "WritePak failed: " << static_cast<int>(res.error) << " " << res.message << std::endl; return 2; }

    AssetError err;
    auto mountOpt = PakMount::Open(outPath, err, true);
    if (!mountOpt) { std::cerr << "Open failed: " << static_cast<int>(err) << std::endl; return 3; }
    PakMount mount = std::move(*mountOpt);

    auto missingId = static_cast<AssetId>(XxHash64(std::string("no/such/asset")));
    if (mount.Has(missingId)) { std::cerr << "Has() unexpectedly found missing asset" << std::endl; return 4; }

    auto info = mount.GetInfo(missingId);
    if (info) { std::cerr << "GetInfo unexpectedly returned info for missing asset" << std::endl; return 5; }

    AssetError loadErr;
    auto blob = mount.Load(missingId, loadErr, true);
    if (blob) { std::cerr << "Load unexpectedly returned blob for missing asset" << std::endl; return 6; }
    if (loadErr != AssetError::NotFound) { std::cerr << "Expected NotFound error" << std::endl; return 7; }

    std::cout << "Missing asset test passed." << std::endl;
    return 0;
}
