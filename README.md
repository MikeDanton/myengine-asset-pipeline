# Asset Pack Pipeline

## Overview

## Design Goals

## Non-Goals

## High-Level Architecture

## Pack File Format (`.pak`)

## Asset Identity and Lookup

## Asset Types

## Offline Tool: `assetc`

## Runtime Library: `asset_runtime`

## Mounting and Asset Resolution

## Compression and Hashing

## Versioning and Compatibility

## Development Workflow

## Testing and Validation

## Integration into a Game Engine

## Performance Characteristics

## Limitations and Future Work

## Building
This project provides a small, standalone asset pipeline library and a CLI tool for writing `.pak` files.

Prerequisites
- CMake >= 3.16
- A C++17-capable compiler (GCC/Clang/MSVC)
- Optional: zstd development headers (`libzstd-dev`) to enable compression tests and runtime compression support

On Debian/Ubuntu install the zstd dev package if you want compression enabled in tests and runtime:

```bash
sudo apt update
sudo apt install -y build-essential cmake libzstd-dev
```

To configure and build the project:

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

After running `cmake --install` the project exports a CMake package configuration in
`<prefix>/lib/cmake/asset_pipeline` which allows downstream projects to use:

```cmake
find_package(AssetPipeline REQUIRED)
target_link_libraries(myexe PRIVATE AssetPipeline::asset_runtime)
```

If you prefer not to install system-wide, use `CMAKE_PREFIX_PATH` to point to an
installation prefix where the package was installed.

Development notes
- The project vendors `xxHash` and `BLAKE3` (via CMake FetchContent) to provide stable, deterministic
	`XxHash64` for asset ids and BLAKE3-128 for content hashes. `zstd` is optional and by default uses the
	system-provided development package when available.

For CI, either install `libzstd-dev` in the runner image or change the CMake config to vendor `zstd`.

## License
