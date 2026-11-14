// xxhash64.h
#pragma once

#include <cstdint>
#include <string>

// XXH64 with seed 0, used as the stable AssetId contract by both the packer
// and runtime.
namespace asset {

std::uint64_t XxHash64(const std::string& s) noexcept;

} // namespace asset
