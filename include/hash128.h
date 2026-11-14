// hash128.h
#pragma once

#include "pak_format.h"

#include <cstddef>

namespace asset::pak {

// Compute a 128-bit content hash for `data` with length `size`.
// BLAKE3 truncated to 128 bits.
Hash128 ComputeHash128(const void* data, std::size_t size) noexcept;

} // namespace asset::pak
