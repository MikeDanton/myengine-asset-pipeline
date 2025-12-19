// pak_writer.cpp
#include "pak_writer.h"
#include "pak_format.h"
#include "xxhash64.h"
#include "hash128.h"
#include "compression_zstd.h"

#include "io_utils.h"

#include <fstream>
#include <cstring>
#include <system_error>
#include <sstream>

namespace asset {

PakWriteResult WritePak(const std::string& outPath, const std::vector<PakWriterEntry>& entries) {
    std::ofstream os(outPath, std::ios::binary);
    if (!os) return PakWriteResult::Fail(PakWriteError::FileOpenFailed, "failed to open output file");

    // Write placeholder header (64 bytes)
    pak::PakHeaderWire hdr{};
    std::memcpy(hdr.magic, pak::PAK_MAGIC, sizeof(pak::PAK_MAGIC));
    hdr.format_version = pak::PAK_FORMAT_VERSION;
    hdr.header_size = static_cast<std::uint32_t>(pak::PakHeaderSize());
    hdr.toc_offset = 0;
    hdr.toc_count = 0;
    hdr.toc_entry_size = static_cast<std::uint32_t>(pak::PakTocEntrySize());
    hdr.toc_hash = {};
    if (!asset::util::write_struct(os, hdr)) return PakWriteResult::Fail(PakWriteError::IoError, "failed to write header placeholder");

    // Keep TOC entries in memory; write blobs first
    std::vector<pak::PakTocEntryWire> toc;
    toc.reserve(entries.size());

    std::uint64_t current_offset = static_cast<std::uint64_t>(pak::PakHeaderSize());

    for (const auto& e : entries) {
        pak::PakTocEntryWire te{};
        te.id = static_cast<pak::AssetId>(XxHash64(e.canonical_key));
        te.type = static_cast<std::uint16_t>(e.type);
        te.type_version = e.type_version;
        te.flags = e.flags;
        te.compression = static_cast<std::uint8_t>(e.compression);
        te.data_offset = current_offset;

        // compute content hash of raw bytes
        if (!e.bytes.empty()) {
            te.content_hash = pak::ComputeHash128(e.bytes.data(), e.bytes.size());
        }

        // write blob (optionally compress)
        if (e.compression == pak::Compression::Zstd) {
            std::vector<uint8_t> compressed;
            if (!CompressZstd(e.bytes.empty() ? nullptr : e.bytes.data(), e.bytes.size(), compressed)) {
                return PakWriteResult::Fail(PakWriteError::CompressUnavailable, "zstd compression unavailable or failed");
            }
            te.raw_size = static_cast<std::uint64_t>(e.bytes.size());
            te.stored_size = static_cast<std::uint64_t>(compressed.size());
            if (!compressed.empty()) {
                os.write(reinterpret_cast<const char*>(compressed.data()), static_cast<std::streamsize>(compressed.size()));
                if (!os) return PakWriteResult::Fail(PakWriteError::IoError, "failed to write compressed blob");
            }
        } else {
            te.raw_size = static_cast<std::uint64_t>(e.bytes.size());
            te.stored_size = te.raw_size;
            if (!e.bytes.empty()) {
                os.write(reinterpret_cast<const char*>(e.bytes.data()), static_cast<std::streamsize>(e.bytes.size()));
                if (!os) return PakWriteResult::Fail(PakWriteError::IoError, "failed to write raw blob");
            }
        }

        current_offset += te.stored_size;
        toc.push_back(te);
    }

    // Now write TOC at current offset
    std::uint64_t toc_offset = current_offset;
    std::vector<std::uint8_t> toc_bytes;
    toc_bytes.reserve(toc.size() * pak::PakTocEntrySize());

    for (const auto& te : toc) {
        std::ostringstream oss;
        asset::util::write_struct(oss, te);
        const std::string s = oss.str();
        toc_bytes.insert(toc_bytes.end(), s.begin(), s.end());
    }

    // compute toc hash
    pak::Hash128 toc_hash = pak::ComputeHash128(toc_bytes.data(), toc_bytes.size());

    // write raw TOC bytes to file
    if (!toc_bytes.empty()) {
        os.write(reinterpret_cast<const char*>(toc_bytes.data()), static_cast<std::streamsize>(toc_bytes.size()));
        if (!os) return PakWriteResult::Fail(PakWriteError::IoError, "failed to write TOC bytes");
        current_offset += static_cast<std::uint64_t>(toc_bytes.size());
    }

    // finalize header and seek back to write header with correct TOC offset
    hdr.toc_offset = toc_offset;
    hdr.toc_count = static_cast<std::uint32_t>(toc.size());
    hdr.toc_entry_size = static_cast<std::uint32_t>(pak::PakTocEntrySize());
    hdr.toc_hash = toc_hash;

    os.seekp(0, std::ios::beg);
    if (!os) return PakWriteResult::Fail(PakWriteError::SeekFailed, "failed to seek to header position");
    if (!asset::util::write_struct(os, hdr)) return PakWriteResult::Fail(PakWriteError::IoError, "failed to write finalized header");

    os.flush();
    if (!os.good()) return PakWriteResult::Fail(PakWriteError::IoError, "stream in bad state after flush");
    return PakWriteResult::Ok();
}

} // namespace asset

