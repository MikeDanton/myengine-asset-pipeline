#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <sstream>
#include "xxhash64.h"
#include "hash128.h"

using namespace asset;

static std::string to_hex_u64(uint64_t v) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0') << std::setw(16) << v;
    return oss.str();
}

static std::string to_hex_bytes(const pak::Hash128& h) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (size_t i = 0; i < sizeof(h.bytes); ++i) {
        oss << std::setw(2) << static_cast<int>(h.bytes[i]);
    }
    return oss.str();
}

int main() {
    // Extended test vectors: empty, simple, pangram, long, non-ASCII (UTF-8), binary
    const std::vector<std::string> inputs = {
        "",
        "hello",
        "The quick brown fox jumps over the lazy dog",
        std::string(1000, 'a'), // long repeated 'a'
        "こんにちは", // Japanese greeting in UTF-8
        std::string("\x00\x01\x02\xff", 4) // binary bytes including NUL
    };
    for (const auto &s : inputs) {
        uint64_t id = XxHash64(s);
        pak::Hash128 hh = pak::ComputeHash128(s.data(), s.size());
        std::cout << "INPUT: '" << s << "'\n";
        std::cout << "XXH64: " << to_hex_u64(id) << "\n";
        std::cout << "BLAKE3-128: " << to_hex_bytes(hh) << "\n\n";
    }
    return 0;
}
