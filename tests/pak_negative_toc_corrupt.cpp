// pak_negative_toc_corrupt.cpp
// Creates a valid pak then corrupts the header TOC count to trigger TocCorrupt on Open.

#include <iostream>
#include <vector>
#include <string>
#include <fstream>

#include "pak_writer.h"
#include "asset_runtime.h"
#include "pak_format.h"
#include "io_utils.h"

auto main() -> int {
    using namespace asset;

    std::vector<PakWriterEntry> entries;
    PakWriterEntry a;
    a.canonical_key = "neg/one";
    a.type = pak::AssetType::RawBytes;
    a.bytes = { 'x', 'y', 'z' };
    entries.push_back(a);

    const std::string outPath = "test_neg_toc.pak";
    auto res = WritePak(outPath, entries);
    if (!res) { std::cerr << "WritePak failed: " << static_cast<int>(res.error) << " " << res.message << '\n'; return 2; }

    // Corrupt header.toc_count
    {
        std::fstream f(outPath, std::ios::binary | std::ios::in | std::ios::out);
        if (!f) { std::cerr << "Open for corrupt failed" << '\n'; return 3; }
        pak::PakHeaderWire hdr{};
        if (!asset::util::read_struct(f, hdr)) { std::cerr << "Read header failed" << '\n'; return 4; }
        hdr.toc_count = hdr.toc_count + 10; // obviously wrong
        f.seekp(0, std::ios::beg);
        asset::util::write_struct(f, hdr);
        f.flush();
    }

    AssetError err = AssetError::Ok;
    auto mount = PakMount::Open(outPath, err, false);
    if (mount) { std::cerr << "Expected Open to fail due to corrupt TOC" << '\n'; return 5; }
    if (err != AssetError::TocCorrupt) { std::cerr << "Unexpected error: " << static_cast<int>(err) << '\n'; return 6; }

    std::cout << "TocCorrupt test passed." << '\n';
    return 0;
}
