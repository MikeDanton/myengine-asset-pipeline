// pak_roundtrip_zstd.cpp
// Writes a small pak with compressed blobs using Zstd, then reads them back
// and verifies bytes and content hash verification.

#include <iostream>
#include <vector>
#include <string>

#include "pak_writer.h"
#include "asset_runtime.h"
#include "xxhash64.h"

auto main() -> int {
    using namespace asset;

    std::vector<PakWriterEntry> entries;
    PakWriterEntry entryA;
    entryA.canonical_key = "zstd/one";
    entryA.type = pak::AssetType::RawBytes;
    entryA.compression = pak::Compression::Zstd;
    constexpr std::size_t kSizeA = 1024;
    {
        const std::string strA = std::string(kSizeA, 'A'); // 1KB of A's compressible
        entryA.bytes.assign(strA.begin(), strA.end());
    }
    entries.push_back(entryA);

    PakWriterEntry entryB;
    entryB.canonical_key = "zstd/two";
    entryB.type = pak::AssetType::RawBytes;
    entryB.compression = pak::Compression::Zstd;
    constexpr std::size_t kSizeB = 2048;
    {
        const std::string strB = std::string(kSizeB, 'B'); // 2KB of B's
        entryB.bytes.assign(strB.begin(), strB.end());
    }
    entries.push_back(entryB);

    const std::string outPath = "test_roundtrip_zstd.pak";
    // named return codes for clarity in tests
    constexpr int RC_WRITE_FAILED = 2;
    constexpr int RC_LOAD_A_FAILED = 5;
    constexpr int RC_LOAD_B_FAILED = 6;
    constexpr int RC_SIZE_MISMATCH = 7;
    constexpr int RC_CONTENT_MISMATCH = 8;
    auto res = WritePak(outPath, entries);
    if (!res) { std::cerr << "WritePak failed: " << static_cast<int>(res.error) << " " << res.message << '\n'; return RC_WRITE_FAILED; }

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
    if (!blobAOpt) { std::cerr << "Load A failed: " << static_cast<int>(loadErr) << '\n'; return RC_LOAD_A_FAILED; }
    auto blobBOpt = mountRef.Load(idB, loadErr, true);
    if (!blobBOpt) { std::cerr << "Load B failed: " << static_cast<int>(loadErr) << '\n'; return RC_LOAD_B_FAILED; }

    if (blobAOpt->bytes.size() != kSizeA || blobBOpt->bytes.size() != kSizeB) {
        std::cerr << "Decompressed size mismatch" << '\n';
        return RC_SIZE_MISMATCH;
    }

    // quick content checks
    if (blobAOpt->bytes[0] != 'A' || blobBOpt->bytes[0] != 'B') {
        std::cerr << "Contents mismatch after decompress" << '\n';
        return RC_CONTENT_MISMATCH;
    }

    std::cout << "Zstd roundtrip test passed." << '\n';
    return 0;
}
