// pak_roundtrip_full.cpp
// Writes a small pak with two blobs, then reads them back and verifies bytes.

#include <iostream>
#include <vector>
#include <string>
#include <cassert>

#include "pak_writer.h"
#include "asset_runtime.h"
#include "xxhash64.h"

int main() {
    using namespace asset;

    std::vector<PakWriterEntry> entries;
    PakWriterEntry a;
    a.canonical_key = "test/one";
    a.type = pak::AssetType::RawBytes;
    {
        const std::string s = "hello world";
        a.bytes.assign(s.begin(), s.end());
    }
    entries.push_back(a);

    PakWriterEntry b;
    b.canonical_key = "test/two";
    b.type = pak::AssetType::RawBytes;
    {
        const std::string s = "goodbye";
        b.bytes.assign(s.begin(), s.end());
    }
    entries.push_back(b);

    const std::string outPath = "test_roundtrip.pak";
    auto res = WritePak(outPath, entries);
    if (!res) {
        std::cerr << "WritePak failed: " << static_cast<int>(res.error) << " " << res.message << std::endl;
        return 2;
    }

    AssetError err;
    auto mountOpt = PakMount::Open(outPath, err, true);
    if (!mountOpt) {
        std::cerr << "PakMount::Open failed: " << static_cast<int>(err) << std::endl;
        return 3;
    }
    PakMount mount = std::move(*mountOpt);

    auto idA = static_cast<AssetId>(XxHash64(a.canonical_key));
    auto idB = static_cast<AssetId>(XxHash64(b.canonical_key));

    if (!mount.Has(idA) || !mount.Has(idB)) {
        std::cerr << "Has() failed to find entries" << std::endl;
        return 4;
    }

    AssetError loadErr;
    auto blobAOpt = mount.Load(idA, loadErr, true);
    if (!blobAOpt) { std::cerr << "Load A failed: " << static_cast<int>(loadErr) << std::endl; return 5; }
    auto blobBOpt = mount.Load(idB, loadErr, true);
    if (!blobBOpt) { std::cerr << "Load B failed: " << static_cast<int>(loadErr) << std::endl; return 6; }

    std::string sa(blobAOpt->bytes.begin(), blobAOpt->bytes.end());
    std::string sb(blobBOpt->bytes.begin(), blobBOpt->bytes.end());

    if (sa != "hello world" || sb != "goodbye") {
        std::cerr << "Contents mismatch: '" << sa << "' vs 'hello world'" << std::endl;
        return 7;
    }

    std::cout << "Roundtrip test passed." << std::endl;
    return 0;
}
