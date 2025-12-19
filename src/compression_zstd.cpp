// compression_zstd.cpp
// ZSTD compress/decompress wrapper. If compiled without libzstd available
// these functions will return false.

#include <cstdint>
#include <cstddef>
#include <vector>
#include <cstring>

#if defined(ASSET_HAS_ZSTD) && ASSET_HAS_ZSTD
#include <zstd.h>
#endif

namespace asset {

bool DecompressZstd(const void* src, size_t srcSize, std::vector<uint8_t>& out) {
#if defined(ASSET_HAS_ZSTD) && ASSET_HAS_ZSTD
    if (!src || srcSize == 0) { out.clear(); return true; }
    unsigned long long bound = ZSTD_getFrameContentSize(src, srcSize);
    if (bound == ZSTD_CONTENTSIZE_ERROR || bound == ZSTD_CONTENTSIZE_UNKNOWN) {
        // Unknown content size: attempt a decompression into a growing buffer
        size_t estimated = srcSize * 3; // heuristic
        out.resize(estimated);
        size_t decompressed = ZSTD_decompress(out.data(), out.size(), src, srcSize);
        if (ZSTD_isError(decompressed)) return false;
        out.resize(decompressed);
        return true;
    }
    out.resize(static_cast<size_t>(bound));
    size_t decompressed = ZSTD_decompress(out.data(), out.size(), src, srcSize);
    if (ZSTD_isError(decompressed)) return false;
    out.resize(decompressed);
    return true;
#else
    (void)src; (void)srcSize; (void)out; return false;
#endif
}

bool CompressZstd(const void* src, size_t srcSize, std::vector<uint8_t>& out) {
#if defined(ASSET_HAS_ZSTD) && ASSET_HAS_ZSTD
    if (!src || srcSize == 0) { out.clear(); return true; }
    size_t bound = ZSTD_compressBound(srcSize);
    out.resize(bound);
    int compressionLevel = 3; // reasonable default
    size_t written = ZSTD_compress(out.data(), out.size(), src, srcSize, compressionLevel);
    if (ZSTD_isError(written)) return false;
    out.resize(written);
    return true;
#else
    (void)src; (void)srcSize; (void)out; return false;
#endif
}

} // namespace asset
