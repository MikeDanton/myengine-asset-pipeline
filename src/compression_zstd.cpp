// compression_zstd.cpp
// ZSTD compress/decompress wrapper. If compiled without libzstd available
// these functions will return false.

#include <cstdint>
#include <cstddef>
#include <vector>
#include <cstring>
#include <istream>

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

bool DecompressZstdTo(const void* src, size_t srcSize, void* dst, size_t dstCapacity, size_t& outWritten) {
#if defined(ASSET_HAS_ZSTD) && ASSET_HAS_ZSTD
    if (!src || srcSize == 0) { outWritten = 0; return true; }
    // If content size is known, prefer that
    unsigned long long contentSize = ZSTD_getFrameContentSize(src, srcSize);
    if (contentSize != ZSTD_CONTENTSIZE_ERROR && contentSize != ZSTD_CONTENTSIZE_UNKNOWN) {
        if (contentSize > dstCapacity) return false;
        size_t decompressed = ZSTD_decompress(dst, dstCapacity, src, srcSize);
        if (ZSTD_isError(decompressed)) return false;
        outWritten = decompressed;
        return true;
    }

    // Content size unknown: attempt decompression into provided buffer. If the
    // buffer is too small ZSTD_decompress will return an error.
    size_t decompressed = ZSTD_decompress(dst, dstCapacity, src, srcSize);
    if (ZSTD_isError(decompressed)) return false;
    outWritten = decompressed;
    return true;
#else
    (void)src; (void)srcSize; (void)dst; (void)dstCapacity; (void)outWritten; return false;
#endif
}

bool DecompressZstdStreamTo(std::istream& is, size_t srcSize, void* dst, size_t dstCapacity, size_t& outWritten) {
#if defined(ASSET_HAS_ZSTD) && ASSET_HAS_ZSTD
    if (srcSize == 0) { outWritten = 0; return true; }
    ZSTD_DCtx* dctx = ZSTD_createDCtx();
    if (!dctx) return false;

    const size_t kInBuf = 1 << 16; // 64 KiB input chunks
    size_t inBufSize = (srcSize < kInBuf) ? srcSize : kInBuf;
    std::vector<char> inBuf(inBufSize);
    ZSTD_inBuffer input{inBuf.data(), 0, 0};

    uint8_t* outPtr = static_cast<uint8_t*>(dst);
    size_t outPos = 0;
    size_t remaining = srcSize;

    while (true) {
        if (input.pos == input.size && remaining > 0) {
            size_t toRead = (remaining < inBuf.size()) ? remaining : inBuf.size();
            is.read(inBuf.data(), static_cast<std::streamsize>(toRead));
            std::streamsize got = is.gcount();
            if (got <= 0) { ZSTD_freeDCtx(dctx); return false; }
            input.src = inBuf.data();
            input.size = static_cast<size_t>(got);
            input.pos = 0;
            remaining -= static_cast<size_t>(got);
        }

        ZSTD_outBuffer output{ outPtr + outPos, dstCapacity - outPos, 0 };
        size_t ret = ZSTD_decompressStream(dctx, &output, &input);
        if (ZSTD_isError(ret)) { ZSTD_freeDCtx(dctx); return false; }
        outPos += output.pos;

        // finished frame
        if (ret == 0) break;

        // if output buffer is full but frame not finished -> dst too small
        if (outPos == dstCapacity && ret != 0) { ZSTD_freeDCtx(dctx); return false; }
    }

    outWritten = outPos;
    ZSTD_freeDCtx(dctx);
    return true;
#else
    (void)is; (void)srcSize; (void)dst; (void)dstCapacity; (void)outWritten; return false;
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
