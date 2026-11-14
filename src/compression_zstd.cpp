// compression_zstd.cpp
// ZSTD compress/decompress wrapper. If compiled without libzstd available
// these functions will return false.

#include <cstdint>
#include <cstddef>
#include <vector>
#include <cstring>
#include <istream>
#include <memory>

#if defined(ASSET_HAS_ZSTD) && ASSET_HAS_ZSTD
#include <zstd.h>
#endif

namespace asset {

bool DecompressZstd(const void* src, size_t srcSize, std::vector<uint8_t>& out) {
#if defined(ASSET_HAS_ZSTD) && ASSET_HAS_ZSTD
    out.clear();
    if (!src && srcSize > 0) return false;
    if (srcSize == 0) return true;
    ZSTD_DCtx* dctx = ZSTD_createDCtx();
    if (!dctx) return false;
    std::unique_ptr<ZSTD_DCtx, decltype(&ZSTD_freeDCtx)> owner(dctx, &ZSTD_freeDCtx);
    ZSTD_inBuffer input{src, srcSize, 0};
    std::vector<uint8_t> chunk(ZSTD_DStreamOutSize());
    while (true) {
        ZSTD_outBuffer output{chunk.data(), chunk.size(), 0};
        const size_t previous = input.pos;
        const size_t ret = ZSTD_decompressStream(dctx, &output, &input);
        if (ZSTD_isError(ret)) { out.clear(); return false; }
        out.insert(out.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(output.pos));
        if (ret == 0) {
            const bool complete = input.pos == input.size;
            if (!complete) out.clear();
            return complete;
        }
        if (input.pos == previous && output.pos == 0) {
            out.clear(); return false;
        }
    }
#else
    (void)src; (void)srcSize; (void)out; return false;
#endif
}

bool DecompressZstdTo(const void* src, size_t srcSize, void* dst, size_t dstCapacity, size_t& outWritten) {
#if defined(ASSET_HAS_ZSTD) && ASSET_HAS_ZSTD
    outWritten = 0;
    if ((!src && srcSize > 0) || (!dst && dstCapacity > 0)) return false;
    if (srcSize == 0) return true;
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
    outWritten = 0;
    if (!dst && dstCapacity > 0) return false;
    if (srcSize == 0) return true;
    ZSTD_DCtx* dctx = ZSTD_createDCtx();
    if (!dctx) return false;
    std::unique_ptr<ZSTD_DCtx, decltype(&ZSTD_freeDCtx)> owner(dctx, &ZSTD_freeDCtx);

    const size_t kInBuf = 1 << 16; // 64 KiB input chunks
    size_t inBufSize = (srcSize < kInBuf) ? srcSize : kInBuf;
    std::vector<char> inBuf(inBufSize);
    ZSTD_inBuffer input{inBuf.data(), 0, 0};

    uint8_t empty = 0;
    uint8_t* outPtr = dst ? static_cast<uint8_t*>(dst) : &empty;
    size_t outPos = 0;
    size_t remaining = srcSize;

    while (true) {
        if (input.pos == input.size && remaining > 0) {
            size_t toRead = (remaining < inBuf.size()) ? remaining : inBuf.size();
            is.read(inBuf.data(), static_cast<std::streamsize>(toRead));
            std::streamsize got = is.gcount();
            if (got <= 0) { return false; }
            input.src = inBuf.data();
            input.size = static_cast<size_t>(got);
            input.pos = 0;
            remaining -= static_cast<size_t>(got);
        }

        const size_t previousInput = input.pos;
        ZSTD_outBuffer output{ outPtr + outPos, dstCapacity - outPos, 0 };
        size_t ret = ZSTD_decompressStream(dctx, &output, &input);
        if (ZSTD_isError(ret)) { return false; }
        outPos += output.pos;

        // finished frame
        if (ret == 0) {
            if (remaining != 0 || input.pos != input.size) { return false; }
            break;
        }
        if (input.pos == previousInput && output.pos == 0) { return false; }

    }

    outWritten = outPos;
    return true;
#else
    (void)is; (void)srcSize; (void)dst; (void)dstCapacity; (void)outWritten; return false;
#endif
}

bool CompressZstd(const void* src, size_t srcSize, std::vector<uint8_t>& out) {
#if defined(ASSET_HAS_ZSTD) && ASSET_HAS_ZSTD
    if (!src && srcSize > 0) return false;
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
