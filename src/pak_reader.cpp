// pak_reader.cpp
#include "asset_runtime.h"
#include "pak_format.h"
#include "xxhash64.h"
#include "hash128.h"
#include "compression_zstd.h"

#include <fstream>
#include <cstring>
#include <algorithm>

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

PakMount::PakMount(PakMount&& o) noexcept : impl_(o.impl_) { o.impl_ = nullptr; }
PakMount& PakMount::operator=(PakMount&& o) noexcept { if (this != &o) { delete impl_; impl_ = o.impl_; o.impl_ = nullptr; } return *this; }

PakMount::~PakMount() { delete impl_; impl_ = nullptr; }

std::optional<PakMount> PakMount::Open(const std::string& path, AssetError& out_err, bool verify_toc_hash) {
    out_err = AssetError::Ok;
    std::ifstream is(path, std::ios::binary | std::ios::ate);
    if (!is) {
        out_err = AssetError::FileOpenFailed;
        return std::nullopt;
    }

    const std::streamsize file_size = is.tellg();
    is.seekg(0, std::ios::beg);

    pak::PakHeaderWire hdr{};
    is.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
    if (!is) { out_err = AssetError::IoError; return std::nullopt; }

    if (std::memcmp(hdr.magic, pak::PAK_MAGIC, sizeof(pak::PAK_MAGIC)) != 0) {
        out_err = AssetError::BadMagic;
        return std::nullopt;
    }

    if (hdr.format_version != pak::PAK_FORMAT_VERSION) {
        out_err = AssetError::UnsupportedVersion;
        return std::nullopt;
    }

    Impl* impl = new Impl();
    impl->path = path;
    impl->header = hdr;
    impl->header_read = true;

    // Read TOC if present
    if (hdr.toc_offset != 0 && hdr.toc_count > 0) {
        const std::uint64_t expected_toc_size = static_cast<std::uint64_t>(hdr.toc_count) * hdr.toc_entry_size;
        if (hdr.toc_offset + expected_toc_size > static_cast<std::uint64_t>(file_size)) {
            delete impl; out_err = AssetError::TocCorrupt; return std::nullopt;
        }

        is.seekg(static_cast<std::streamoff>(hdr.toc_offset), std::ios::beg);
        impl->toc_entries.resize(hdr.toc_count);
        for (std::uint32_t i = 0; i < hdr.toc_count; ++i) {
            pak::PakTocEntryWire te{};
            is.read(reinterpret_cast<char*>(&te), sizeof(te));
            if (!is) { delete impl; out_err = AssetError::IoError; return std::nullopt; }
            impl->toc_entries[i] = te;
        }

        if (verify_toc_hash) {
            // Compute hash of TOC bytes and compare
            std::vector<uint8_t> toc_bytes;
            toc_bytes.reserve(expected_toc_size);
            for (const auto& te : impl->toc_entries) {
                const uint8_t* ptr = reinterpret_cast<const uint8_t*>(&te);
                toc_bytes.insert(toc_bytes.end(), ptr, ptr + sizeof(te));
            }
            pak::Hash128 calc = pak::ComputeHash128(toc_bytes.data(), toc_bytes.size());
            if (!Hash128Equals(calc, hdr.toc_hash)) {
                delete impl; out_err = AssetError::TocCorrupt; return std::nullopt;
            }
        }
    }

    return std::optional<PakMount>(PakMount(impl));
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
        is.read(reinterpret_cast<char*>(stored.data()), stored.size());
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

} // namespace asset
