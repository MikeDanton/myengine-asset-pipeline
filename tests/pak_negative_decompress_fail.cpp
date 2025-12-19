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

int main() {
    using namespace asset;

    std::vector<PakWriterEntry> entries;
    PakWriterEntry a;
    a.canonical_key = "dec/one";
    a.type = pak::AssetType::RawBytes;
    std::string payload = "simple-data";
    a.bytes.assign(payload.begin(), payload.end());
    entries.push_back(a);

    const std::string outPath = "test_neg_decompress.pak";
    auto res = WritePak(outPath, entries);
    if (!res) { std::cerr << "WritePak failed: " << static_cast<int>(res.error) << " " << res.message << std::endl; return 2; }

    // Locate TOC entry and flip compression byte to Zstd
    {
        std::fstream f(outPath, std::ios::binary | std::ios::in | std::ios::out);
        if (!f) { std::cerr << "Open failed" << std::endl; return 3; }
        pak::PakHeaderWire hdr{};
        f.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
        if (!f) { std::cerr << "Read hdr failed" << std::endl; return 4; }
        f.seekg(static_cast<std::streamoff>(hdr.toc_offset), std::ios::beg);
        for (std::uint32_t i = 0; i < hdr.toc_count; ++i) {
            std::streamoff entry_pos = static_cast<std::streamoff>(hdr.toc_offset + i * hdr.toc_entry_size);
            f.seekg(entry_pos, std::ios::beg);
            pak::PakTocEntryWire local{};
            f.read(reinterpret_cast<char*>(&local), sizeof(local));
            if (!f) { std::cerr << "Read toc entry failed" << std::endl; return 5; }
            if (local.id == static_cast<pak::AssetId>(XxHash64(a.canonical_key))) {
                // mark as zstd (but blob remains raw bytes)
                local.compression = static_cast<std::uint8_t>(pak::Compression::Zstd);
                f.seekp(entry_pos, std::ios::beg);
                f.write(reinterpret_cast<const char*>(&local), sizeof(local));
                f.flush();
                break;
            }
        }
    }

    AssetError err;
    // We intentionally modified the TOC without recomputing the toc_hash, so
    // disable TOC hash verification when opening.
    auto mountOpt = PakMount::Open(outPath, err, false);
    if (!mountOpt) { std::cerr << "Open failed: " << static_cast<int>(err) << std::endl; return 6; }
    PakMount mount = std::move(*mountOpt);

    AssetError loadErr;
    auto blob = mount.Load(static_cast<AssetId>(XxHash64(a.canonical_key)), loadErr, true);
    if (blob) { std::cerr << "Expected decompression to fail but got blob" << std::endl; return 7; }
    if (loadErr != AssetError::DecompressFailed) { std::cerr << "Unexpected error: " << static_cast<int>(loadErr) << std::endl; return 8; }

    std::cout << "DecompressFailed test passed." << std::endl;
    return 0;
}
