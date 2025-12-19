// pak_reader.cpp
#include "asset_runtime.h"
#include "pak_format.h"
#include "xxhash64.h"
#include "hash128.h"
#include "compression_zstd.h"
#include "io_utils.h"

#include <fstream>
#include <cstring>
#include <algorithm>
#include <memory>
#include <sstream>

namespace asset {

struct PakMount::Impl {
    std::string path;
    pak::PakHeaderWire header{};
    bool header_read = false;
    std::vector<pak::PakTocEntryWire> toc_entries;
};

static bool Hash128Equals(const pak::Hash128& a, const pak::Hash128& b) noexcept {
    return std::memcmp(a.bytes, b.bytes, sizeof(a.bytes)) == 0;
}

PakMount::PakMount(Impl* impl) : impl_(impl) {}

PakMount::~PakMount() {
    delete impl_;
    impl_ = nullptr;
}

PakMount::PakMount(PakMount&& other) noexcept : impl_(other.impl_) {
    other.impl_ = nullptr;
}

PakMount& PakMount::operator=(PakMount&& other) noexcept {
    if (this != &other) {
        delete impl_;
        impl_ = other.impl_;
        other.impl_ = nullptr;
    }
    return *this;
}

std::unique_ptr<PakMount> PakMount::Open(const std::string& path, AssetError& out_err, bool verify_toc_hash) {
    out_err = AssetError::Ok;
    std::ifstream is(path, std::ios::binary | std::ios::ate);
        if (!is) { 
            out_err = AssetError::FileOpenFailed; 
            return nullptr; 
    }

    const std::streamsize file_size = is.tellg();
    is.seekg(0, std::ios::beg);

    pak::PakHeaderWire hdr{};
    if (!asset::util::read_struct(is, hdr)) { out_err = AssetError::IoError; return nullptr; }

    if (std::memcmp(hdr.magic, pak::PAK_MAGIC, sizeof(pak::PAK_MAGIC)) != 0) {
        out_err = AssetError::BadMagic;
        return nullptr;
    }

    if (hdr.format_version != pak::PAK_FORMAT_VERSION) {
        out_err = AssetError::UnsupportedVersion;
        return nullptr;
    }

    std::unique_ptr<Impl> impl_up = std::make_unique<Impl>();
    impl_up->path = path;
    impl_up->header = hdr;
    impl_up->header_read = true;

    // Read TOC if present
    if (hdr.toc_offset != 0 && hdr.toc_count > 0) {
        const std::uint64_t expected_toc_size = static_cast<std::uint64_t>(hdr.toc_count) * hdr.toc_entry_size;
        if (hdr.toc_offset + expected_toc_size > static_cast<std::uint64_t>(file_size)) {
            out_err = AssetError::TocCorrupt; return nullptr;
        }

        is.seekg(static_cast<std::streamoff>(hdr.toc_offset), std::ios::beg);
        impl_up->toc_entries.resize(hdr.toc_count);
        for (std::uint32_t i = 0; i < hdr.toc_count; ++i) {
            pak::PakTocEntryWire te{};
            if (!asset::util::read_struct(is, te)) { out_err = AssetError::IoError; return nullptr; }
            impl_up->toc_entries[i] = te;
        }

            if (verify_toc_hash) {
            // Compute hash of TOC bytes and compare (serialize entries)
            std::vector<uint8_t> toc_bytes;
            toc_bytes.reserve(expected_toc_size);
            for (const auto& te : impl_up->toc_entries) {
                std::ostringstream oss;
                asset::util::write_struct(oss, te);
                const std::string s = oss.str();
                toc_bytes.insert(toc_bytes.end(), s.begin(), s.end());
            }
            pak::Hash128 calc = pak::ComputeHash128(toc_bytes.data(), toc_bytes.size());
            if (!Hash128Equals(calc, hdr.toc_hash)) {
                out_err = AssetError::TocCorrupt; return nullptr;
            }
        }
    }

    return std::unique_ptr<PakMount>(new PakMount(impl_up.release()));
}

bool PakMount::Has(AssetId id) const {
    if (!impl_) return false;
    for (const auto& te : impl_->toc_entries) if (te.id == id) return true;
    return false;
}

std::optional<AssetInfo> PakMount::GetInfo(AssetId id) const {
    if (!impl_) return std::nullopt;
    for (const auto& te : impl_->toc_entries) {
        if (te.id != id) continue;
        AssetInfo ai;
        ai.id = te.id;
        ai.type = static_cast<pak::AssetType>(te.type);
        ai.type_version = te.type_version;
        ai.flags = te.flags;
        ai.compression = static_cast<pak::Compression>(te.compression);
        ai.raw_size = te.raw_size;
        ai.stored_size = te.stored_size;
        return ai;
    }
    return std::nullopt;
}

std::optional<AssetBlob> PakMount::Load(AssetId id, AssetError& out_err, bool verify_content_hash) const {
    out_err = AssetError::Ok;
    if (!impl_) { out_err = AssetError::IoError; return std::nullopt; }

    const pak::PakTocEntryWire* found = nullptr;
    for (const auto& te : impl_->toc_entries) if (te.id == id) { found = &te; break; }
    if (!found) { out_err = AssetError::NotFound; return std::nullopt; }

    std::ifstream is(impl_->path, std::ios::binary);
    if (!is) { out_err = AssetError::FileOpenFailed; return std::nullopt; }

    // Read stored bytes
    is.seekg(static_cast<std::streamoff>(found->data_offset), std::ios::beg);
    std::vector<uint8_t> stored;
    stored.resize(static_cast<size_t>(found->stored_size));
    if (found->stored_size > 0) {
            is.read(reinterpret_cast<char*>(stored.data()), static_cast<std::streamsize>(stored.size()));
        if (!is) { out_err = AssetError::IoError; return std::nullopt; }
    }

    std::vector<uint8_t> out_bytes;
    if (found->compression == static_cast<uint8_t>(pak::Compression::None)) {
        out_bytes = std::move(stored);
    } else if (found->compression == static_cast<uint8_t>(pak::Compression::Zstd)) {
        if (!asset::DecompressZstd(stored.data(), stored.size(), out_bytes)) {
            out_err = AssetError::DecompressFailed; return std::nullopt;
        }
    } else {
        out_err = AssetError::IoError; return std::nullopt;
    }

    if (verify_content_hash) {
        pak::Hash128 calc = pak::ComputeHash128(out_bytes.data(), out_bytes.size());
        if (!Hash128Equals(calc, found->content_hash)) {
            out_err = AssetError::HashMismatch; return std::nullopt;
        }
    }

    AssetBlob blob;
    blob.info.id = found->id;
    blob.info.type = static_cast<pak::AssetType>(found->type);
    blob.info.type_version = found->type_version;
    blob.info.flags = found->flags;
    blob.info.compression = static_cast<pak::Compression>(found->compression);
    blob.info.raw_size = found->raw_size;
    blob.info.stored_size = found->stored_size;
    blob.bytes = std::move(out_bytes);
    return std::optional<AssetBlob>(std::move(blob));
}

std::optional<std::size_t> PakMount::LoadTo(AssetId id,
                                           std::span<std::uint8_t> dst,
                                           AssetInfo* out_info,
                                           AssetError& out_err,
                                           bool verify_content_hash) const {
    out_err = AssetError::Ok;
    if (!impl_) { out_err = AssetError::IoError; return std::nullopt; }

    const pak::PakTocEntryWire* found = nullptr;
    for (const auto& te : impl_->toc_entries) if (te.id == id) { found = &te; break; }
    if (!found) { out_err = AssetError::NotFound; return std::nullopt; }

    // raw_size is the uncompressed size we will write into dst
    if (found->raw_size > static_cast<std::uint64_t>(dst.size())) {
        out_err = AssetError::BufferTooSmall;
        return std::nullopt;
    }

    std::ifstream is(impl_->path, std::ios::binary);
    if (!is) { out_err = AssetError::FileOpenFailed; return std::nullopt; }

    is.seekg(static_cast<std::streamoff>(found->data_offset), std::ios::beg);

    if (found->compression == static_cast<uint8_t>(pak::Compression::None)) {
        if (found->stored_size > 0) {
            is.read(reinterpret_cast<char*>(dst.data()), static_cast<std::streamsize>(found->stored_size));
            if (!is) { out_err = AssetError::IoError; return std::nullopt; }
        }
    } else if (found->compression == static_cast<uint8_t>(pak::Compression::Zstd)) {
        // Decompress directly from file stream into the provided `dst`
        // buffer using zstd streaming API to avoid allocating a large
        // temporary buffer for the compressed frame.
        size_t wrote = 0;
        if (!asset::DecompressZstdStreamTo(is, static_cast<size_t>(found->stored_size), dst.data(), dst.size(), wrote)) {
            out_err = AssetError::DecompressFailed; return std::nullopt;
        }
        if (wrote != static_cast<size_t>(found->raw_size)) {
            out_err = AssetError::IoError; return std::nullopt;
        }
    } else {
        out_err = AssetError::IoError; return std::nullopt;
    }

    // Optional content hash verify
    if (verify_content_hash) {
        pak::Hash128 calc = pak::ComputeHash128(dst.data(), static_cast<size_t>(found->raw_size));
        if (!Hash128Equals(calc, found->content_hash)) {
            out_err = AssetError::HashMismatch; return std::nullopt;
        }
    }

    if (out_info) {
        out_info->id = found->id;
        out_info->type = static_cast<pak::AssetType>(found->type);
        out_info->type_version = found->type_version;
        out_info->flags = found->flags;
        out_info->compression = static_cast<pak::Compression>(found->compression);
        out_info->raw_size = found->raw_size;
        out_info->stored_size = found->stored_size;
    }

    return static_cast<std::size_t>(found->raw_size);
}

} // namespace asset
