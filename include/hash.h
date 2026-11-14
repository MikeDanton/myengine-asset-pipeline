// hash.h
#pragma once

#include <cstdint>
#include <string>

namespace asset {

// FNV-1a 64-bit helper. Pack AssetIds use XxHash64 instead.
uint64_t Hash64(const std::string& s) noexcept;

} // namespace asset
