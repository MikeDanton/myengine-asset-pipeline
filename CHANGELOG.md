# Changelog

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

