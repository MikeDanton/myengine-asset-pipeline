// hash.h
#pragma once

#include <cstdint>
#include <string>

namespace asset {

// Stable 64-bit hash used for AssetId (placeholder implementation).
// Replace with xxhash64 or siphash for production.
uint64_t Hash64(const std::string& s) noexcept;

} // namespace asset
