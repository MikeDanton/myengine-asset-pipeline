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

