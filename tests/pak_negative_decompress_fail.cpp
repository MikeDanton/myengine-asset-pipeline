// pak_negative_decompress_fail.cpp
// Writes a pak with an uncompressed entry then tweaks the TOC to mark it as Zstd
// so that Load() attempts decompression and fails (either because zstd not
// available or because stored bytes are not valid compressed data).

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
    PakWriterEntry entry;
    entry.canonical_key = "dec/one";
    entry.type = pak::AssetType::RawBytes;
    std::string payload = "simple-data";
    entry.bytes.assign(payload.begin(), payload.end());
    entries.push_back(entry);

    const std::string outPath = "test_neg_decompress.pak";
    // Named return codes to avoid magic-number warnings in tests
    constexpr int RC_READ_TOC_FAILED = 5;
    constexpr int RC_OPEN_MOUNT_FAILED = 6;
    constexpr int RC_UNEXPECTED_BLOB = 7;
    constexpr int RC_UNEXPECTED_ERROR = 8;
    auto res = WritePak(outPath, entries);
    if (!res) { std::cerr << "WritePak failed: " << static_cast<int>(res.error) << " " << res.message << '\n'; return 2; }

    // Locate TOC entry and flip compression byte to Zstd
    {
        std::fstream file_stream(outPath, std::ios::binary | std::ios::in | std::ios::out);
        if (!file_stream) { std::cerr << "Open failed" << '\n'; return 3; }
        pak::PakHeaderWire hdr{};
        if (!asset::util::read_struct(file_stream, hdr)) { std::cerr << "Read hdr failed" << '\n'; return 4; }
        file_stream.seekg(static_cast<std::streamoff>(hdr.toc_offset), std::ios::beg);
        for (std::uint32_t i = 0; i < hdr.toc_count; ++i) {
            // perform multiplication in 64-bit to avoid implicit widening
            const std::uint64_t offset_for_i = hdr.toc_offset + (static_cast<std::uint64_t>(i) * static_cast<std::uint64_t>(hdr.toc_entry_size));
            auto entry_pos = static_cast<std::streamoff>(offset_for_i);
            file_stream.seekg(entry_pos, std::ios::beg);
            pak::PakTocEntryWire local{};
            if (!asset::util::read_struct(file_stream, local)) { std::cerr << "Read toc entry failed" << '\n'; return RC_READ_TOC_FAILED; }
            if (local.id == static_cast<pak::AssetId>(XxHash64(entry.canonical_key))) {
                // mark as zstd (but blob remains raw bytes)
                local.compression = static_cast<std::uint8_t>(pak::Compression::Zstd);
                file_stream.seekp(entry_pos, std::ios::beg);
                asset::util::write_struct(file_stream, local);
                file_stream.flush();
                break;
            }
        }
    }

    AssetError err = AssetError::Ok;
    // We intentionally modified the TOC without recomputing the toc_hash, so
    // disable TOC hash verification when opening.
    auto mount = PakMount::Open(outPath, err, false);
    if (!mount) { std::cerr << "Open failed: " << static_cast<int>(err) << '\n'; return RC_OPEN_MOUNT_FAILED; }
    auto& mountRef = *mount;

    AssetError loadErr = AssetError::Ok;
    auto blob = mountRef.Load(static_cast<AssetId>(XxHash64(entry.canonical_key)), loadErr, true);
    if (blob) { std::cerr << "Expected decompression to fail but got blob" << '\n'; return RC_UNEXPECTED_BLOB; }
    if (loadErr != AssetError::DecompressFailed) { std::cerr << "Unexpected error: " << static_cast<int>(loadErr) << '\n'; return RC_UNEXPECTED_ERROR; }

    std::cout << "DecompressFailed test passed." << '\n';
    return 0;
}
