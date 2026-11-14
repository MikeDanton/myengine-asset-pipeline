// hash128.cpp
#include "hash128.h"

#include <blake3.h>

namespace asset::pak {

Hash128 ComputeHash128(const void* data, std::size_t size) noexcept {
    Hash128 out{};
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    if (size > 0 && data != nullptr) {
        blake3_hasher_update(&hasher, data, size);
    }
    blake3_hasher_finalize(&hasher, out.bytes, sizeof(out.bytes));
    return out;
}

} // namespace asset::pak
