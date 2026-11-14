// pak_loadto.cpp
// Tests PakMount::LoadTo for uncompressed and zstd-compressed assets,
// buffer-too-small behavior, and content-hash verification.

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cstddef>

#include "pak_writer.h"
#include "asset_runtime.h"
#include "xxhash64.h"
#include "io_utils.h"
#include "pak_format.h"

int main() {
    using namespace asset;

    std::vector<PakWriterEntry> entries;

    PakWriterEntry rawEntry;
    rawEntry.canonical_key = "raw/one";
    rawEntry.type = pak::AssetType::RawBytes;
    rawEntry.compression = pak::Compression::None;
    constexpr size_t kRawSize = 512;
    rawEntry.bytes.assign(kRawSize, 'R');
    entries.push_back(rawEntry);

#if defined(ASSET_HAS_ZSTD) && ASSET_HAS_ZSTD
    PakWriterEntry zstdEntry;
    zstdEntry.canonical_key = "zstd/one";
    zstdEntry.type = pak::AssetType::RawBytes;
    zstdEntry.compression = pak::Compression::Zstd;
    constexpr size_t kZstdSize = 1024;
    zstdEntry.bytes.assign(kZstdSize, 'Z');
    entries.push_back(zstdEntry);
#endif

    const std::string outPath = "test_loadto.pak";

    auto writeRes = WritePak(outPath, entries);
    if (!writeRes) {
        std::cerr << "WritePak failed: " << static_cast<int>(writeRes.error) << " " << writeRes.message << '\n';
        return 2;
    }

    AssetError err = AssetError::Ok;
    auto mount = PakMount::Open(outPath, err, true);
    if (!mount) {
        std::cerr << "PakMount::Open failed: " << static_cast<int>(err) << '\n';
        return 3;
    }
    auto& m = *mount;

    // Uncompressed load -> LoadTo should succeed and return raw_size
    AssetId rawId = static_cast<AssetId>(XxHash64(rawEntry.canonical_key));
    auto infoOpt = m.GetInfo(rawId);
    if (!infoOpt) { std::cerr << "GetInfo raw failed" << '\n'; return 4; }
    size_t rawSize = static_cast<size_t>(infoOpt->raw_size);
    std::vector<uint8_t> rawDst(rawSize);
    AssetError outErr = AssetError::Ok;
    auto ret = m.LoadTo(rawId, std::span<uint8_t>(rawDst.data(), rawDst.size()), nullptr, outErr, true);
    if (!ret.has_value() || ret.value() != rawSize) { std::cerr << "LoadTo raw failed: " << static_cast<int>(outErr) << '\n'; return 5; }
    if (rawDst[0] != 'R') { std::cerr << "Raw content mismatch" << '\n'; return 6; }

    // Buffer-too-small case
    if (rawSize > 1) {
        std::vector<uint8_t> small(rawSize - 1);
        outErr = AssetError::Ok;
        auto r2 = m.LoadTo(rawId, std::span<uint8_t>(small.data(), small.size()), nullptr, outErr, false);
        if (r2.has_value() || outErr != AssetError::BufferTooSmall) { std::cerr << "Expected BufferTooSmall" << '\n'; return 7; }
    }

#if defined(ASSET_HAS_ZSTD) && ASSET_HAS_ZSTD
    // Zstd streaming path
    AssetId zId = static_cast<AssetId>(XxHash64(zstdEntry.canonical_key));
    auto zinfoOpt = m.GetInfo(zId);
    if (!zinfoOpt) { std::cerr << "GetInfo zstd failed" << '\n'; return 8; }
    size_t zRaw = static_cast<size_t>(zinfoOpt->raw_size);
    std::vector<uint8_t> zDst(zRaw);
    outErr = AssetError::Ok;
    auto r3 = m.LoadTo(zId, std::span<uint8_t>(zDst.data(), zDst.size()), nullptr, outErr, true);
    if (!r3.has_value() || r3.value() != zRaw) { std::cerr << "LoadTo zstd failed: " << static_cast<int>(outErr) << '\n'; return 9; }
    if (zDst[0] != 'Z') { std::cerr << "Zstd content mismatch" << '\n'; return 10; }

    // Now mutate the TOC entry content_hash in the file to force a verify failure
    {
        std::fstream fs(outPath, std::ios::in | std::ios::out | std::ios::binary);
        if (!fs) { std::cerr << "Failed opening pak for toc mutate" << '\n'; return 11; }
        pak::PakHeaderWire hdr{};
        if (!asset::util::read_struct(fs, hdr)) { std::cerr << "Failed read header" << '\n'; return 12; }
        // compute index of zstd entry (it's second)
        size_t idx = 1;
        std::streamoff entryPos = static_cast<std::streamoff>(hdr.toc_offset + idx * hdr.toc_entry_size + offsetof(pak::PakTocEntryWire, content_hash));
        fs.seekp(entryPos, std::ios::beg);
        if (!fs) { std::cerr << "seekp failed" << '\n'; return 13; }
        // Flip a byte rather than assigning a value it might already contain.
        fs.seekg(entryPos, std::ios::beg);
        char c = 0;
        fs.read(&c, 1);
        c = static_cast<char>(static_cast<unsigned char>(c) ^ 1u);
        fs.seekp(entryPos, std::ios::beg);
        fs.write(&c, 1);
        fs.flush();
    }

    // TOC verification must reject the altered hash metadata. Disable only
    // that check to exercise the independent LoadTo content-hash check below.
    auto rejected = PakMount::Open(outPath, err, true);
    if (rejected || err != AssetError::TocCorrupt) {
        std::cerr << "Expected corrupted TOC rejection" << '\n'; return 16;
    }
    auto mount2 = PakMount::Open(outPath, err, false);
    if (!mount2) { std::cerr << "PakMount::Open after mutate failed: " << static_cast<int>(err) << '\n'; return 14; }
    auto& m2 = *mount2;
    std::vector<uint8_t> zDst2(zRaw);
    outErr = AssetError::Ok;
    auto r4 = m2.LoadTo(zId, std::span<uint8_t>(zDst2.data(), zDst2.size()), nullptr, outErr, true);
    if (r4.has_value() || outErr != AssetError::HashMismatch) { std::cerr << "Expected HashMismatch after TOC mutate" << '\n'; return 15; }
#endif

    std::cout << "LoadTo tests passed." << '\n';
    return 0;
}
