# Asset Pack Pipeline

This repository provides a small, standalone asset pipeline: a CLI tool `assetc` to create
`.pak` files and a tiny runtime library `asset_runtime` to mount and read assets at runtime.

**Goals**
- Deterministic AssetId generation using `XxHash64` of a canonical key.
- Content integrity using truncated BLAKE3-128 (128-bit) content hashes.
- Optional `zstd` compression for stored blobs.
- Simple, stable wire format for `.pak` files with clear versioning.
- Strict developer tooling (Werror, clang-tidy) and ASan testing.

**Quick usage**

Build and run the `assetc` tool:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc) --target assetc
./build/tools/assetc/assetc [options]
```

Use the runtime API to open a pack and load an asset:

```cpp
#include "asset_runtime.h"

asset::AssetError err;
auto mount = asset::PakMount::Open("assets.pak", err, /*verify_toc_hash=*/true);
if (!mount) { /* handle error via err */ }

asset::AssetError load_err;
auto blob = mount->Load(someAssetId, load_err, /*verify_content_hash=*/true);
if (blob) {
		// use blob->bytes and blob->info
}
```

Where `PakMount::Open` returns an owning `std::unique_ptr<asset::PakMount>` (or `nullptr` on failure),
and `Load` returns `std::optional<asset::AssetBlob>`; `AssetError` describes error cases.

Wire-format and detailed pack layout are described in `docs/PAK_FORMAT.md` (new).

## Building

Prerequisites
- CMake >= 3.16
- A C++20-capable compiler (GCC/Clang/MSVC)
- Optional: `zstd` development headers (`libzstd-dev`) to enable compression tests and runtime compression support

On Debian/Ubuntu:

```bash
sudo apt update
sudo apt install -y build-essential cmake libzstd-dev
```

To configure and build the project (Release):

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

Run tests (tests that require zstd will be skipped if system `zstd` is not available):

```bash
ctest --test-dir build --verbose
```

Install the library and headers (installs to system locations by default):

```bash
cmake --install build --prefix /usr/local
```

Consuming from another CMake project
-----------------------------------

After `cmake --install`, the project exports a CMake package config in
`<prefix>/lib/cmake/asset_pipeline`, e.g.:

```cmake
find_package(AssetPipeline REQUIRED)
target_link_libraries(myexe PRIVATE AssetPipeline::asset_runtime)
```

Development notes
- This project vendors `xxHash` and `BLAKE3` (via CMake FetchContent) to provide deterministic
	`XxHash64` for asset ids and BLAKE3-128 for content hashes. `zstd` is optional and uses the
	system-provided development package when available.

CI note: Either install `libzstd-dev` in your runner image or configure CMake to vendor `zstd`.

## Documentation

- `include/pak_format.h` — C++ definitions for on-disk wire structs.
- `include/asset_runtime.h` — runtime API types: `PakMount`, `AssetInfo`, `AssetBlob`, `AssetError`.
- `docs/PAK_FORMAT.md` — human-readable wire-format description (new file).

## Using from other projects

You can consume this project from downstream CMake builds in three common ways.

- Install + find_package (recommended for released versions)

	Build and install into a prefix, then `find_package` in your project:

	```bash
	cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
	cmake --build build -j
	cmake --install build --prefix /opt/asset_pipeline
	```

	In your consumer `CMakeLists.txt`:

	```cmake
	find_package(AssetPipeline CONFIG REQUIRED PATHS /opt/asset_pipeline/lib/cmake/asset_pipeline)
	target_link_libraries(myexe PRIVATE AssetPipeline::asset_runtime)
	```

- FetchContent (convenient for CI and per-repo reproducibility)

	```cmake
	include(FetchContent)
	FetchContent_Declare(
		asset_pipeline
		GIT_REPOSITORY https://github.com/<owner>/myengine-asset-pipeline.git
		GIT_TAG        v0.1.0
	)
	FetchContent_MakeAvailable(asset_pipeline)
	target_link_libraries(myexe PRIVATE AssetPipeline::asset_runtime)
	```

- add_subdirectory / Git submodule (useful for local development)

	```cmake
	add_subdirectory(third_party/myengine-asset-pipeline)
	target_link_libraries(myexe PRIVATE AssetPipeline::asset_runtime)
	```

CMake options useful to consumers and integrators (defaults shown):

- `ENABLE_WERROR=ON` — treat warnings as errors during builds (default: `ON`).
- `ENABLE_CLANG_TIDY=OFF` — enable clang-tidy during build when available (default: `OFF`).
- `BUILD_TESTS=ON` — build the test suite (default: `ON`).

Notes:
- The exported target name is `AssetPipeline::asset_runtime` (see `src/CMakeLists.txt`).
- The project already installs a package config and export files so `find_package` works after `cmake --install`.
- `zstd` support is auto-detected; install system `zstd` dev packages to enable compression support at build time.

## License
