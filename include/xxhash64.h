// xxhash64.h
#pragma once

#include <cstdint>
#include <string>

// We use the xxHash64 implementation (XXH64) for fast, stable 64-bit hashes
// used as AssetId. The implementation is provided by the external xxHash
// library (fetched via CMake FetchContent).
namespace asset {

std::uint64_t XxHash64(const std::string& s) noexcept;

} // namespace asset
