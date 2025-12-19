// pak_writer.h
#pragma once

#include "pak_format.h"

#include <string>
#include <vector>

namespace asset {

struct PakWriterEntry {
    std::string canonical_key; // e.g. "textures/stone/granite"
    pak::AssetType type{pak::AssetType::RawBytes};
    std::uint16_t type_version{0};
    std::uint32_t flags{0};
    pak::Compression compression{pak::Compression::None};
    std::vector<std::uint8_t> bytes; // raw (uncompressed) payload
};

// Result of `WritePak` containing an error code and optional message.
enum class PakWriteError {
    Ok = 0,
    FileOpenFailed,
    IoError,
    CompressUnavailable,
    CompressFailed,
    SeekFailed,
    Unknown,
};

struct PakWriteResult {
    PakWriteError error{PakWriteError::Ok};
    std::string message;
    explicit operator bool() const noexcept { return error == PakWriteError::Ok; }
    static PakWriteResult Ok() noexcept { return PakWriteResult{PakWriteError::Ok, {}}; }
    static PakWriteResult Fail(PakWriteError e, std::string msg = {}) { return PakWriteResult{e, std::move(msg)}; }
};

// Write a `.pak` file containing the provided entries. Returns a `PakWriteResult`
// with details on failure. The function writes a header placeholder, writes
// blobs contiguously, then writes the TOC and finalizes the header with the
// TOC offset and hash.
PakWriteResult WritePak(const std::string& outPath,
                       const std::vector<PakWriterEntry>& entries);

} // namespace asset
