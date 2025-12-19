// hash.cpp
#include "hash.h"

namespace asset {

uint64_t Hash64(const std::string& s) noexcept {
    // FNV-1a 64-bit
    uint64_t h = 1469598103934665603ULL;
    for (char ch : s) {
        unsigned char c = static_cast<unsigned char>(ch);
        h ^= static_cast<uint64_t>(c);
        h *= 1099511628211ULL;
    }
    return h;
}

} // namespace asset
