# Asset Pack Pipeline

Last edited: 2026-11-19. Last reviewed: 2026-11-14 (C++ packer and engine integration).

This repository is the source of truth for `asset_runtime` and `assetc`. MyEngine2
consumes this checkout through `add_subdirectory`; make library changes here,
run its tests here, and commit them here. Do not maintain another source copy in
the engine. The last published release is v1.1.0; the changes below are unreleased.

## Build and test

Requires CMake 3.24+, a C++20 compiler and Git. xxHash v0.8.1 and the tested BLAKE3
revision are pinned through FetchContent. Initial configuration needs network
access or existing `FETCHCONTENT_SOURCE_DIR_XXHASH` / `_BLAKE3` source overrides.
System zstd development headers/library are detected automatically; no zstd
source is downloaded. Set `ASSET_PIPELINE_WITH_ZSTD=OFF` to test without compression.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j 4
ctest --test-dir build --output-on-failure -j 4
```

`ENABLE_WERROR=ON` applies to project targets, without changing compiler flags in
consumers or dependencies. `ENABLE_CLANG_TIDY=OFF` is opt-in. Standalone tests
default on; embedded consumers enable them with `ASSET_PIPELINE_BUILD_TESTS=ON`.
Compression-dependent coverage is registered only when zstd is enabled; the
remaining tests include hash vectors, roundtrips, corruption and LoadTo behavior.

## Pack and read

```sh
build/tools/assetc/assetc assets assets.pak
build/tools/assetc/assetc assets assets-compressed.pak --compress
```

`--compress` requires a build with zstd support. The CLI also accepts an optional
cache filename. [Wire format](docs/PAK_FORMAT.md) records the pack layout.
Asset IDs use XXH64 with seed zero; content hashes use BLAKE3 truncated to 128 bits.
The separate `Hash64` utility uses standard FNV-1a; it is not the pack ID algorithm.

```cpp
#include "asset_runtime.h"
#include "xxhash64.h"

asset::AssetError error = asset::AssetError::Ok;
auto mount = asset::PakMount::Open("assets.pak", error, true);
if (mount) {
    auto bytes = mount->Load(asset::XxHash64("textures/stone.png"), error, true);
    // Check bytes/error before use. LoadTo also supports caller-owned buffers.
}
```

`PAK_VERBOSE=1` prints individual TOC records; the default reports a mount summary.

## Consume from CMake

For local engine/library development:

```cmake
add_subdirectory(../myengine-asset-pipeline asset_pipeline)
target_link_libraries(my_app PRIVATE AssetPipeline::asset_runtime)
```

Headers and required link dependencies propagate through the target. Do not copy
sources or add the library's include directory manually. MyEngine2 defaults to the
sibling checkout; `MYENGINE2_ASSET_PIPELINE_SOURCE_DIR` selects a different checkout.

To install and consume a package:

```sh
cmake --install build --prefix /path/to/prefix
```

```cmake
find_package(AssetPipeline CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE AssetPipeline::asset_runtime)
```

Set `CMAKE_PREFIX_PATH` to that prefix. Installation includes `assetc`, runtime
headers/library and the pinned BLAKE3 package; zstd-enabled packages additionally
require zstd development files on the consuming machine. The config finds these
transitive dependencies. No release tag has been created for the migration.

## Runtime and compiler review (2026-11-14)

Pack parsing checks header/entry sizes, overflow-safe offsets, payload bounds,
raw/stored size consistency, supported compression and unique IDs independently
of optional hash verification. `Load` and `LoadTo` share bounded decoding; truncated
or trailing zstd streams fail without a no-progress loop. Empty assets have valid
BLAKE3 hashes, including compressed empty frames. The implementation supports
little-endian hosts; the wire format has not changed.

The CLI sorts exact case-preserving keys before packing. Its versioned cache
includes compression policy and the output digest, so option changes or corrupted
outputs force rebuilding. Output and cache files inside the input directory are
excluded. A private temporary pack replaces the output only after successful
writing. Python 3, when available, enables six CLI regression scenarios in CTest.
The pack safety suite covers malformed lengths, empty assets, truncated frames,
trailing bytes and unknown-content-size zstd frames.

## Reviewed asset sources (human and AI)

The upstream Python `tools/assetprep.py` command stages approved PNG artwork from
separate `human-file` and `ai-import` adapters, validates source/asset keys and
basic PNG structure, and creates a deterministic provenance sidecar **outside**
the staging tree. It does not call image-generation APIs or modify the existing
`.pak` wire format. After staging, pass the output directory to `assetc` as usual.

See [Source workflow](docs/SOURCE_WORKFLOW.md) for the manifest contract,
review/rights requirements, staging commands and next milestones.

```sh
python3 -m unittest discover -s tests -p test_assetprep.py
```

## Deterministic sprite atlases

After `assetprep` approves human/AI source PNGs, the optional offline
`tools/atlasprep.py` (Python + Pillow) packs sprite strips into an RGBA atlas,
extrudes edge padding, and emits per-frame UV rectangles and pivots as JSON.
Pass its pack-ready output to the existing `assetc`; runtime does not need
Python, Pillow, AI services or a new wire format. See
[atlas workflow](docs/ATLAS_WORKFLOW.md) for a layout manifest and commands.

## Offline candidate generation

`tools/assetgenerate.py` introduces a provider-neutral build-time generation
contract with a deterministic mock provider. It produces **unreviewed**
quarantined candidates, an auditable prompt digest and explicit cost metadata.
Only reviewed and manually approved PNGs may pass through `assetprep`.
`assetc` refuses any input tree containing the quarantine marker, even with
a valid incremental cache. No network generation service or provider SDK is
installed. See [generation workflow](docs/GENERATION_WORKFLOW.md).

## Candidate preview sheets

The optional `tools/candidate_review.py` creates a review-only contact sheet
from quarantined generated PNGs, verifies every candidate against its SHA-256
receipt, and emits another quarantine marker to keep previews out of `assetc`.
It never approves or promotes content. See
[candidate review](docs/CANDIDATE_REVIEW.md) for commands and safety boundaries.
