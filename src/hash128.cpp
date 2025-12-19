// hash128.cpp
#include "hash128.h"

#include <cstdint>
#include <cstring>
#include <blake3.h>

namespace asset::pak {

Hash128 ComputeHash128(const void* data, std::size_t size) noexcept {
    Hash128 out{};
    // BLAKE3 produces 32 bytes by default; we use the first 16 bytes (128 bits)
    // as the content hash stored on-disk.
    unsigned char digest[32];
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    if (size > 0 && data != nullptr) {
        blake3_hasher_update(&hasher, static_cast<const uint8_t*>(data), size);
    }
    blake3_hasher_finalize(&hasher, digest, sizeof(digest));
    std::memcpy(out.bytes, digest, 16);
    return out;
}

} // namespace asset::pak
