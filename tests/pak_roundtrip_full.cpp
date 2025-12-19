// pak_roundtrip_full.cpp
// Writes a small pak with two blobs, then reads them back and verifies bytes.

#include <iostream>
#include <vector>
#include <string>
#include <cassert>

#include "pak_writer.h"
#include "asset_runtime.h"
#include "xxhash64.h"

auto main() -> int {
    using namespace asset;

    std::vector<PakWriterEntry> entries;
    PakWriterEntry entryA;
    entryA.canonical_key = "test/one";
    entryA.type = pak::AssetType::RawBytes;
    {
        const std::string s = "hello world";
        entryA.bytes.assign(s.begin(), s.end());
    }
    entries.push_back(entryA);

    PakWriterEntry entryB;
    entryB.canonical_key = "test/two";
    entryB.type = pak::AssetType::RawBytes;
    {
        const std::string s = "goodbye";
        entryB.bytes.assign(s.begin(), s.end());
    }
    entries.push_back(entryB);

    const std::string outPath = "test_roundtrip.pak";
    auto res = WritePak(outPath, entries);
    if (!res) {
        std::cerr << "WritePak failed: " << static_cast<int>(res.error) << " " << res.message << '\n';
        return 2;
    }

    AssetError err = AssetError::Ok;
    auto mount = PakMount::Open(outPath, err, true);
    if (!mount) {
        std::cerr << "PakMount::Open failed: " << static_cast<int>(err) << '\n';
        return 3;
    }
    auto& mountRef = *mount;

    auto idA = static_cast<AssetId>(XxHash64(entryA.canonical_key));
    auto idB = static_cast<AssetId>(XxHash64(entryB.canonical_key));

    if (!mountRef.Has(idA) || !mountRef.Has(idB)) {
        std::cerr << "Has() failed to find entries" << '\n';
        return 4;
    }

    AssetError loadErr = AssetError::Ok;
    auto blobAOpt = mountRef.Load(idA, loadErr, true);
    if (!blobAOpt) { std::cerr << "Load A failed: " << static_cast<int>(loadErr) << '\n'; return 5; }
    auto blobBOpt = mountRef.Load(idB, loadErr, true);
    if (!blobBOpt) { std::cerr << "Load B failed: " << static_cast<int>(loadErr) << '\n'; return 6; }

    std::string sa(blobAOpt->bytes.begin(), blobAOpt->bytes.end());
    std::string sb(blobBOpt->bytes.begin(), blobBOpt->bytes.end());

    if (sa != "hello world" || sb != "goodbye") {
        std::cerr << "Contents mismatch: '" << sa << "' vs 'hello world'" << '\n';
        return 7;
    }

    std::cout << "Roundtrip test passed." << '\n';
    return 0;
}
