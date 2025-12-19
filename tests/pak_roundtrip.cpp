// pak_roundtrip.cpp
// Minimal test that verifies header/TOC constants and sizes are consistent.

#include <iostream>
#include <cassert>
#include <cstring>

#include "pak_format.h"

int main() {
    using namespace asset::pak;
    std::cout << "Pak format version: " << PAK_FORMAT_VERSION << "\n";
    std::cout << "PakHeader size: " << sizeof(PakHeaderWire) << " bytes\n";
    std::cout << "PakTocEntry size: " << sizeof(PakTocEntryWire) << " bytes\n";

    // Basic sanity: magic length
    static_assert(sizeof(PAK_MAGIC) == 8, "PAK_MAGIC must be 8 bytes");

    // Ensure struct sizes match helper functions
    assert(sizeof(PakHeaderWire) == PakHeaderSize());
    assert(sizeof(PakTocEntryWire) == PakTocEntrySize());

    // Check compile-time assertion in header passes
    std::cout << "Sanity checks passed.\n";
    return 0;
}
