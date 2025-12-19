// hash128.h
#pragma once

#include "pak_format.h"

#include <cstddef>

namespace asset::pak {

// Compute a 128-bit content hash for `data` with length `size`.
// This is a simple, deterministic implementation (two 64-bit FNV variants)
// used as a placeholder for BLAKE3; it is *not* cryptographically strong.
Hash128 ComputeHash128(const void* data, std::size_t size) noexcept;

} // namespace asset::pak
