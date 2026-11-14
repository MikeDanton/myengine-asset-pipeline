// xxhash64.cpp
#include "xxhash64.h"

#include <xxhash.h>

namespace asset {

std::uint64_t XxHash64(const std::string& s) noexcept {
    return static_cast<std::uint64_t>(XXH64(s.data(), s.size(), 0));
}

} // namespace asset
