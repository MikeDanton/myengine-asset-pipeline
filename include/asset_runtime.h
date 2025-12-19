// asset_runtime.h
// Minimal C++ runtime interface for mounting and reading .pak files.

#pragma once

#include "pak_format.h"

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <optional>

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
    static std::optional<PakMount> Open(const std::string& path,
                                        AssetError& out_err,
                                        bool verify_toc_hash = true);

    // Lookup only (no IO beyond TOC).
    [[nodiscard]] bool Has(AssetId id) const;
    [[nodiscard]] std::optional<AssetInfo> GetInfo(AssetId id) const;

    // Load full asset bytes (decompress + optional hash verify).
    std::optional<AssetBlob> Load(AssetId id,
                                  AssetError& out_err,
                                  bool verify_content_hash = true) const;

private:
    struct Impl;
    Impl* impl_ = nullptr;
    explicit PakMount(Impl*);
};

} // namespace asset

