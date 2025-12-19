// compression_zstd.h
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include <istream>

namespace asset {

// Decompress `src` (size `srcSize`) into `out`. Returns true on success.
bool DecompressZstd(const void* src, size_t srcSize, std::vector<uint8_t>& out);

// Decompress `src` (size `srcSize`) directly into `dst` buffer of capacity
// `dstCapacity`. On success `outWritten` is set to the number of bytes written
// (uncompressed size) and the function returns true. Returns false on failure.
bool DecompressZstdTo(const void* src, size_t srcSize, void* dst, size_t dstCapacity, size_t& outWritten);

// Decompress from an input stream containing exactly `srcSize` compressed bytes
// into the provided `dst` buffer (capacity `dstCapacity`). Writes the number
// of decompressed bytes into `outWritten`. This avoids allocating a full
// temporary buffer for the compressed frame when the compressed size is large.
bool DecompressZstdStreamTo(std::istream& is, size_t srcSize, void* dst, size_t dstCapacity, size_t& outWritten);

// Compress `src` into `out`. Returns true on success.
bool CompressZstd(const void* src, size_t srcSize, std::vector<uint8_t>& out);

} // namespace asset
