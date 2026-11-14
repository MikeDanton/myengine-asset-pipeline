# Changelog

Last edited: 2026-11-14.

## Unreleased

- Harden pack structural validation and bounded Load/LoadTo decoding; reject
  inconsistent sizes, unsupported compression and duplicate IDs before use.
- Fix empty-asset hashes, compressed empty frames, unknown-size zstd decoding,
  and truncated/trailing stream handling with progress checks.
- Make CLI output deterministic and caching sensitive to compression and output
  integrity; avoid ingesting its own output/cache and preserve output on failed writes.
- Add malformed-pack, decompression and CLI regression suites.


- Recover engine-local fixes: standard FNV-1a offset basis, direct BLAKE3-128 output,
  accurate hash contracts and optional verbose pack diagnostics. Pack IDs remain XXH64.
- Pin BLAKE3 and xxHash, isolate warning/tool settings to library targets, and
  provide the same namespaced target for embedding and installed consumers.
- Detect optional system zstd correctly; repair compressed LoadTo test includes
  and distinguish TOC integrity rejection from content-hash rejection.
- Install assetc and complete package dependency discovery; isolate test working
  directories for parallel execution and add FNV-1a regression vectors.
- Make this standalone repository authoritative for MyEngine2 integration.


## v1.0.0 - 2025-12-19

- Initial stable release.
- Core features:
  - Deterministic AssetId via `XxHash64`.
  - Content integrity via BLAKE3-128 (truncated) in TOC.
  - Optional per-asset `zstd` compression.
  - Stable on-disk `.pak` wire format with header/TOC layout.
- Developer tooling:
  - Clang-tidy integration (optional), AddressSanitizer builds available.
  - CI/release checklist added in `docs/CI_RELEASE.md`.
- Documentation:
  - `README.md` expanded with usage and consumption examples.
  - `docs/PAK_FORMAT.md` added describing the wire format.


  ## v1.1.0 - 2025-12-19

  - Added zero-copy load API `PakMount::LoadTo(AssetId, std::span<uint8_t>, ...)` allowing callers
    to reuse buffers and avoid repeated allocations when streaming many assets.
  - Implemented streaming zstd decompression helper `asset::DecompressZstdStreamTo(...)` which
    decompresses directly from an input stream into caller-provided buffers to avoid allocating
    a temporary compressed or decompressed buffer for large frames.
  - Added `tests/pak_loadto.cpp` covering `LoadTo` behaviour for uncompressed and zstd assets,
    small-buffer failures (`AssetError::BufferTooSmall`), and content-hash verification success
    and failure.
  - Added targeted clang-tidy suppressions for test targets to reduce low-value noise
    (e.g. `readability-identifier-length`) while keeping high-value static analyzers enabled.
  - Vendor-fetch of `zstd` via CMake `FetchContent` so the project can build zstd when a system
    package is not present, matching the existing pattern for `xxhash` and `blake3`.

