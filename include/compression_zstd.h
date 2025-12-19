// compression_zstd.h
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace asset {

// Decompress `src` (size `srcSize`) into `out`. Returns true on success.
bool DecompressZstd(const void* src, size_t srcSize, std::vector<uint8_t>& out);

// Compress `src` into `out`. Returns true on success.
bool CompressZstd(const void* src, size_t srcSize, std::vector<uint8_t>& out);

} // namespace asset
