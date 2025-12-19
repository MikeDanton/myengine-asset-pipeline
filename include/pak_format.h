// pak_format.h
// On-disk pack (.pak) format definitions and portable structs
// Designed as a stable wire format. Implementation/IO belongs in .cpp files.

#pragma once

#include <cstdint>
#include <cstddef>

namespace asset::pak {

// 8-byte magic at file start: "MYPAK\0\0\1"
inline constexpr char PAK_MAGIC[8] = {'M','Y','P','A','K','\0','\0','\1'};

// Current pack format version (bump only for incompatible changes).
inline constexpr std::uint32_t PAK_FORMAT_VERSION = 1;

using AssetId = std::uint64_t;

// Keep enums narrow and explicit.
enum class AssetType : std::uint16_t {
    Unknown = 0,
    RawBytes = 1,
    Texture2D = 2,
    ShaderSpirv = 3,
    Json = 10,
    Yaml = 11,
};

enum class Compression : std::uint8_t {
    None = 0,
    Zstd = 1,
};

// Per-entry flags (bitfield). Keep stable.
enum TocFlags : std::uint32_t {
    TocFlag_None        = 0,
    TocFlag_Encrypted   = 1u << 0,
    TocFlag_Streamable  = 1u << 1,
};

// 16-byte hash (e.g., BLAKE3 truncated to 128-bit).
struct Hash128 {
    std::uint8_t bytes[16]{};
};

// NOTE: These wire structs assume little-endian fields on disk.

#pragma pack(push, 1)

// 64 bytes total.
struct PakHeaderWire {
    char magic[8]{};                 // PAK_MAGIC
    std::uint32_t format_version{};  // PAK_FORMAT_VERSION
    std::uint32_t header_size{};     // sizeof(PakHeaderWire)

    std::uint64_t toc_offset{};      // byte offset to TOC array
    std::uint32_t toc_count{};       // number of entries
    std::uint32_t toc_entry_size{};  // sizeof(PakTocEntryWire)

    Hash128 toc_hash;              // hash of TOC bytes (entry array)
    std::uint8_t reserved[16]{};   // future use
};
static_assert(sizeof(PakHeaderWire) == 64, "PakHeaderWire must be 64 bytes");

// 64 bytes total.
struct PakTocEntryWire {
    AssetId id{};                    // hash64(canonical_key)
    std::uint16_t type{};            // AssetType
    std::uint16_t type_version{};    // version of the asset payload schema
    std::uint32_t flags{};           // TocFlags bitfield

    std::uint8_t compression{};      // Compression
    std::uint8_t reserved0[3]{};

    std::uint64_t data_offset{};     // byte offset to blob
    std::uint64_t stored_size{};     // bytes stored in file (compressed)
    std::uint64_t raw_size{};        // bytes after decompression

    Hash128 content_hash;          // hash of *raw* bytes (after decompression)
    std::uint8_t reserved1[4]{};   // pad to 64
};
static_assert(sizeof(PakTocEntryWire) == 64, "PakTocEntryWire must be 64 bytes");

#pragma pack(pop)

inline constexpr std::size_t PakHeaderSize()   { return sizeof(PakHeaderWire); }
inline constexpr std::size_t PakTocEntrySize() { return sizeof(PakTocEntryWire); }

// Canonical key rules (contract): forward slashes, lowercase, no leading '/'.
// AssetId is computed as xxhash64(canonical_key).

} // namespace asset::pak

