// pak_hash_regression.cpp
// Regression tests to lock XxHash64 and BLAKE3-128 outputs for known inputs.

#include <iostream>
#include <string>
#include <iomanip>
#include <sstream>

#include "xxhash64.h"
#include "hash128.h"
#include <vector>

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
    struct Case { std::string input; std::string xxh64; std::string blake3_128; };

    // Prepare inputs including longer, non-ASCII and binary sequences
    std::vector<Case> cases;
    cases.push_back({"", "ef46db3751d8e999", "af1349b9f5f9a1a6a0404dea36dcc949"});
    cases.push_back({"hello", "26c7827d889f6da3", "ea8f163db38682925e4491c5e58d4bb3"});
    cases.push_back({"The quick brown fox jumps over the lazy dog", "0b242d361fda71bc", "2f1514181aadccd913abd94cfa592701"});
    // long repeated 'a' (1000 bytes)
    std::string long_a(1000, 'a');
    cases.push_back({long_a, "56e43b712eda4223", "9957a9014733dd6b6e2f6abcbe7b259a"});
    // non-ASCII UTF-8
    std::string jp = "こんにちは";
    cases.push_back({jp, "9de3be20874e0e76", "6437619010c6a2de7e1f3351c1098e3a"});
    // binary data including NUL
    std::string bin = std::string("\x00\x01\x02\xff", 4);
    cases.push_back({bin, "d184d3c63d2708e8", "10f847936eb4f56573613478660da66b"});

    for (const auto &c : cases) {
        uint64_t id = XxHash64(c.input);
        pak::Hash128 hh = pak::ComputeHash128(c.input.data(), c.input.size());
        const auto xxh_hex = to_hex_u64(id);
        const auto hh_hex = to_hex_bytes(hh);
        if (xxh_hex != c.xxh64) {
            std::cerr << "XxHash64 mismatch for input '" << c.input << "': got " << xxh_hex << " expected " << c.xxh64 << "\n";
            return 1;
        }
        if (hh_hex != c.blake3_128) {
            std::cerr << "BLAKE3-128 mismatch for input '" << c.input << "': got " << hh_hex << " expected " << c.blake3_128 << "\n";
            return 2;
        }
    }

    std::cout << "Hash regression test passed." << std::endl;
    return 0;
}
