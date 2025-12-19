// asset_runtime.h
// Minimal C++ runtime interface for mounting and reading .pak files.

#pragma once

#include "pak_format.h"

#include <cstdint>
#include <string>
#include <vector>
#include <optional>
#include <memory>
#include <span>

namespace asset {

using AssetId = pak::AssetId;

enum class AssetError {
    Ok = 0,
    FileOpenFailed,
    BadMagic,
    UnsupportedVersion,
    IoError,
    TocCorrupt,
    NotFound,
    DecompressFailed,
    BufferTooSmall,
    HashMismatch,
};

struct AssetInfo {
    AssetId id{};
    pak::AssetType type{pak::AssetType::Unknown};
    std::uint16_t type_version{};
    std::uint32_t flags{};
    pak::Compression compression{pak::Compression::None};
    std::uint64_t raw_size{};
    std::uint64_t stored_size{};
};

struct AssetBlob {
    AssetInfo info{};
    std::vector<std::uint8_t> bytes; // raw (decompressed) bytes
};

class PakMount {
public:
    PakMount() = default;
    ~PakMount();

    PakMount(const PakMount&) = delete;
    PakMount& operator=(const PakMount&) = delete;

    PakMount(PakMount&&) noexcept;
    PakMount& operator=(PakMount&&) noexcept;

    // Open and parse TOC. If verify_toc_hash is true, validate toc_hash.
    static std::unique_ptr<PakMount> Open(const std::string& path,
                                          AssetError& out_err,
                                          bool verify_toc_hash = true);

    // Lookup only (no IO beyond TOC).
    [[nodiscard]] bool Has(AssetId id) const;
    [[nodiscard]] std::optional<AssetInfo> GetInfo(AssetId id) const;

    // Load full asset bytes (decompress + optional hash verify).
    std::optional<AssetBlob> Load(AssetId id,
                                  AssetError& out_err,
                                  bool verify_content_hash = true) const;

    // Load asset bytes into a caller-provided buffer `dst`.
    // On success returns the number of bytes written (uncompressed size).
    // On failure returns std::nullopt and sets `out_err`.
    // If `out_info` is non-null it will be filled with metadata for the asset.
    std::optional<std::size_t> LoadTo(AssetId id,
                                      std::span<std::uint8_t> dst,
                                      AssetInfo* out_info,
                                      AssetError& out_err,
                                      bool verify_content_hash = true) const;

private:
    struct Impl;
    Impl* impl_ = nullptr;
    explicit PakMount(Impl* impl);
};

} // namespace asset

