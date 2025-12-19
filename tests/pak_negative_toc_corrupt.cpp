// pak_negative_toc_corrupt.cpp
// Creates a valid pak then corrupts the header TOC count to trigger TocCorrupt on Open.

#include <iostream>
#include <vector>
#include <string>
#include <fstream>

#include "pak_writer.h"
#include "asset_runtime.h"
#include "pak_format.h"

int main() {
    using namespace asset;

    std::vector<PakWriterEntry> entries;
    PakWriterEntry a;
    a.canonical_key = "neg/one";
    a.type = pak::AssetType::RawBytes;
    a.bytes = { 'x', 'y', 'z' };
    entries.push_back(a);

    const std::string outPath = "test_neg_toc.pak";
    auto res = WritePak(outPath, entries);
    if (!res) { std::cerr << "WritePak failed: " << static_cast<int>(res.error) << " " << res.message << std::endl; return 2; }

    // Corrupt header.toc_count
    {
        std::fstream f(outPath, std::ios::binary | std::ios::in | std::ios::out);
        if (!f) { std::cerr << "Open for corrupt failed" << std::endl; return 3; }
        pak::PakHeaderWire hdr{};
        f.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
        if (!f) { std::cerr << "Read header failed" << std::endl; return 4; }
        hdr.toc_count = hdr.toc_count + 10; // obviously wrong
        f.seekp(0, std::ios::beg);
        f.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
        f.flush();
    }

    AssetError err;
    auto mount = PakMount::Open(outPath, err, false);
    if (mount) { std::cerr << "Expected Open to fail due to corrupt TOC" << std::endl; return 5; }
    if (err != AssetError::TocCorrupt) { std::cerr << "Unexpected error: " << static_cast<int>(err) << std::endl; return 6; }

    std::cout << "TocCorrupt test passed." << std::endl;
    return 0;
}
