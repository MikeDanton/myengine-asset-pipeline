// pak_roundtrip_zstd.cpp
// Writes a small pak with compressed blobs using Zstd, then reads them back
// and verifies bytes and content hash verification.

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
    a.canonical_key = "zstd/one";
    a.type = pak::AssetType::RawBytes;
    a.compression = pak::Compression::Zstd;
    {
        const std::string s = std::string(1024, 'A'); // 1KB of A's compressible
        a.bytes.assign(s.begin(), s.end());
    }
    entries.push_back(a);

    PakWriterEntry b;
    b.canonical_key = "zstd/two";
    b.type = pak::AssetType::RawBytes;
    b.compression = pak::Compression::Zstd;
    {
        const std::string s = std::string(2048, 'B'); // 2KB of B's
        b.bytes.assign(s.begin(), s.end());
    }
    entries.push_back(b);

    const std::string outPath = "test_roundtrip_zstd.pak";
    auto res = WritePak(outPath, entries);
    if (!res) { std::cerr << "WritePak failed: " << static_cast<int>(res.error) << " " << res.message << std::endl; return 2; }

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

    if (blobAOpt->bytes.size() != 1024 || blobBOpt->bytes.size() != 2048) {
        std::cerr << "Decompressed size mismatch" << std::endl;
        return 7;
    }

    // quick content checks
    if (blobAOpt->bytes[0] != 'A' || blobBOpt->bytes[0] != 'B') {
        std::cerr << "Contents mismatch after decompress" << std::endl;
        return 8;
    }

    std::cout << "Zstd roundtrip test passed." << std::endl;
    return 0;
}
