#include "asset_runtime.h"
#include "pak_writer.h"
#include "hash128.h"
#include "xxhash64.h"
#include "io_utils.h"
#include "compression_zstd.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <limits>
#include <functional>

#if ASSET_HAS_ZSTD
#include <zstd.h>
#endif

using namespace asset;
static const char* file = "safety.pak";
static bool rejected(const std::function<void(pak::PakHeaderWire&, pak::PakTocEntryWire&)>& mutate) {
    PakWriterEntry entry;
    entry.canonical_key = "fixture";
    entry.bytes = {1, 2, 3, 4};
    if (!WritePak(file, {entry})) return false;
    std::fstream stream(file, std::ios::binary | std::ios::in | std::ios::out);
    pak::PakHeaderWire header{};
    pak::PakTocEntryWire toc{};
    if (!util::read_struct(stream, header)) return false;
    stream.seekg(static_cast<std::streamoff>(header.toc_offset));
    if (!util::read_struct(stream, toc)) return false;
    const auto tocOffset = header.toc_offset;
    mutate(header, toc);
    stream.seekp(0);
    util::write_struct(stream, header);
    stream.seekp(static_cast<std::streamoff>(tocOffset));
    util::write_struct(stream, toc);
    stream.close();
    AssetError error;
    // Safety is required even when integrity verification is disabled.
    return !PakMount::Open(file, error, false) && error == AssetError::TocCorrupt;
}
int main() {
    const std::function<void(pak::PakHeaderWire&, pak::PakTocEntryWire&)> cases[] = {
        [](auto& h, auto&) { h.toc_offset = std::numeric_limits<uint64_t>::max() - 1; },
        [](auto& h, auto&) { h.toc_entry_size = 0; h.toc_count = 0xffffffffu; },
        [](auto& h, auto&) { h.header_size = 0; },
        [](auto& h, auto&) { h.toc_count = 0; h.toc_offset = 0; },
        [](auto&, auto& t) { t.raw_size = 1; }, // formerly overwrote a one-byte LoadTo span
        [](auto&, auto& t) { t.stored_size = std::numeric_limits<uint64_t>::max(); },
        [](auto&, auto& t) { t.data_offset = std::numeric_limits<uint64_t>::max(); },
        [](auto&, auto& t) { t.compression = 255; },
    };
    for (const auto& test : cases) if (!rejected(test)) { std::cerr << "Malformed pack accepted\n"; return 1; }
    PakWriterEntry empty;
    empty.canonical_key = "empty";
    if (!WritePak(file, {empty})) return 2;
    AssetError error;
    auto mount = PakMount::Open(file, error, true);
    if (!mount) return 3;
    auto loaded = mount->Load(XxHash64("empty"), error, true);
    if (!loaded || !loaded->bytes.empty()) return 4;
    if (!mount->LoadTo(XxHash64("empty"), {}, nullptr, error, true)) return 5;
    if (WritePak(file, {empty, empty})) return 6;
    if (!PakMount::Open(file, error, true)) return 7; // rejected write preserved old file
    if (!WritePak(file, {})) return 8;
    if (!PakMount::Open(file, error, true)) return 9;
#if ASSET_HAS_ZSTD
    // A caller may reuse a larger buffer. Valid loads and malformed compressed
    // frames must both preserve the suffix beyond the declared raw size.
    PakWriterEntry bounded;
    bounded.canonical_key = "bounded";
    bounded.compression = pak::Compression::Zstd;
    bounded.bytes = {1, 2, 3, 4};
    if (!WritePak(file, {bounded})) return 20;
    const auto boundedId = XxHash64("bounded");
    mount = PakMount::Open(file, error, true);
    if (!mount) return 21;
    constexpr uint8_t canary = 0xA5;
    std::vector<uint8_t> oversized(8, canary);
    auto count = mount->LoadTo(boundedId, oversized, nullptr, error, true);
    if (!count || *count != bounded.bytes.size() || error != AssetError::Ok) return 22;
    for (size_t i = 0; i < oversized.size(); ++i) {
        const auto expected = i < bounded.bytes.size() ? bounded.bytes[i] : canary;
        if (oversized[i] != expected) return 23;
    }
    mount.reset();
    // Underdeclare the raw size without modifying the compressed frame.
    {
        std::fstream stream(file, std::ios::binary | std::ios::in | std::ios::out);
        pak::PakHeaderWire header{};
        pak::PakTocEntryWire toc{};
        if (!util::read_struct(stream, header)) return 24;
        stream.seekg(static_cast<std::streamoff>(header.toc_offset));
        if (!util::read_struct(stream, toc)) return 25;
        toc.raw_size = 2;
        stream.seekp(static_cast<std::streamoff>(header.toc_offset));
        util::write_struct(stream, toc);
        stream.close();
        if (!stream) return 26;
    }
    mount = PakMount::Open(file, error, false);
    if (!mount) return 27;
    oversized.assign(8, canary);
    count = mount->LoadTo(boundedId, oversized, nullptr, error, false);
    if (count || error != AssetError::DecompressFailed) return 28;
    for (size_t i = 2; i < oversized.size(); ++i) {
        if (oversized[i] != canary) return 29;
    }

    std::vector<uint8_t> compressed;
    std::string text(10000, 'x');
    if (!CompressZstd(text.data(), text.size(), compressed)) return 10;
    std::vector<uint8_t> output(text.size() + 1);
    size_t written = 0;
    for (size_t cut = 1; cut < compressed.size(); ++cut) {
        std::istringstream stream(std::string(reinterpret_cast<const char*>(compressed.data()), cut));
        if (DecompressZstdStreamTo(stream, cut, output.data(), output.size(), written)) return 11;
    }
    std::vector<uint8_t> decoded;
    if (!DecompressZstd(compressed.data(), compressed.size(), decoded) || decoded.size() != text.size()) return 12;
    // Frame without a declared output size must also decode correctly.
    auto* context = ZSTD_createCCtx();
    if (!context) return 16;
    if (ZSTD_isError(ZSTD_CCtx_setParameter(context, ZSTD_c_contentSizeFlag, 0))) return 17;
    std::vector<uint8_t> unknown(ZSTD_compressBound(text.size()));
    const auto size = ZSTD_compress2(context, unknown.data(), unknown.size(), text.data(), text.size());
    ZSTD_freeCCtx(context);
    if (ZSTD_isError(size)) return 18;
    unknown.resize(size);
    if (!DecompressZstd(unknown.data(), unknown.size(), decoded) || decoded.size() != text.size()) return 19;
    compressed.push_back(0);
    std::istringstream trailing(std::string(reinterpret_cast<const char*>(compressed.data()), compressed.size()));
    if (DecompressZstdStreamTo(trailing, compressed.size(), output.data(), output.size(), written)) return 13;
    empty.compression = pak::Compression::Zstd;
    if (!WritePak(file, {empty})) return 14;
    mount = PakMount::Open(file, error, true);
    if (!mount || !mount->Load(XxHash64("empty"), error, true) ||
        !mount->LoadTo(XxHash64("empty"), {}, nullptr, error, true)) return 15;
#endif
    std::cout << "Pack safety regressions passed\n";
}
