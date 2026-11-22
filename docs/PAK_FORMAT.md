# .pak Wire Format

This document describes the implemented **version 1** on-disk `.pak` wire format
used by the asset pipeline. Reviewed against `include/pak_format.h`,
`src/pak_writer.cpp` and `src/pak_reader.cpp` on 2026-11-22. It is versioned,
but **the current reader rejects unknown versions and struct sizes**; no
forward/backward compatibility beyond the implemented v1 contract is implied.

## High-level layout

File layout (all multi-byte integer fields are little-endian):

- `PakHeaderWire` (exactly 64 bytes)
- Blob data (zero or more stored asset blobs, written back-to-back)
- `PakTocEntryWire` array (exactly 64 bytes per entry)

The header contains offsets and sizes that point to the TOC and version info.

## PakHeader (C representation)

See `include/pak_format.h` for the packed `PakHeaderWire` definition.
The v1 header is **64 bytes** and currently contains:

- `magic[8]` — exactly `MYPAK` followed by bytes `00 00 01` (`PAK_MAGIC`), **not** an arbitrary ASCII identifier.
- `format_version` (uint32) — exactly `1` (`PAK_FORMAT_VERSION`).
- `header_size` (uint32) — exactly `64`, checked by the reader.
- `toc_offset` (uint64) — file offset of the TOC; at least 64 even for an empty pack. A temporary zero during writing is a placeholder, not a valid finalized pack.
- `toc_count` (uint32) — number of TOC entries; zero is supported.
- `toc_entry_size` (uint32) — exactly `64`, checked by the reader.
- `toc_hash` (16 bytes) — BLAKE3-128 of the raw TOC bytes. The writer computes it even for an empty TOC.
- `reserved[16]` — reserved bytes, zero-filled by the current writer.

All fields are stored in little-endian.

## TOC entry (PakTocEntryWire)

Each v1 TOC entry is a fixed-size **64-byte packed** `PakTocEntryWire` with:

- `id` (uint64) — AssetId computed as `XxHash64(canonical_key)`.
- `type` (uint16) — Asset type identifier (project-defined enum).
- `type_version` (uint16) — Version of the type layout.
- `flags` (uint32) — `TocFlags` bitfield (`Encrypted`, `Streamable`, or zero); bit width is **32**, not 16.
- `compression` (uint8) — `0 = None`, `1 = Zstd` (other values reserved).
- `reserved0[3]` immediately after compression, and `reserved1[4]` at the end; all are included in the 64-byte serialized entry.
- `data_offset` (uint64) — offset to the stored bytes for this asset.
- `raw_size` (uint64) — uncompressed size (0 for empty asset).
- `stored_size` (uint64) — stored size (after compression if compressed).
- `content_hash` (16 bytes) — BLAKE3-128 of the raw (uncompressed) bytes.

TOC entries are serialized using the explicit one-byte-packed wire structs.
The writer computes the hash over the concatenated 64-byte entries. The
reader checks all header/entry sizes, TOC offsets, payload bounds, duplicate
IDs and supported compression values even when hashing is disabled; with
`verify_toc_hash=true` it also recomputes and compares the TOC digest.

## Hashing and identity

- AssetId: `XxHash64` over a canonical UTF-8 asset key string (the writer is
  responsible for defining and using a canonical key for each asset).
- Content hash: `BLAKE3-128` (first 128 bits of the BLAKE3 output) computed over
  the raw uncompressed bytes. This is stored in the TOC entry and used for
  optional verification at load time.

## Compression

- Compression is optional per-entry. Current supported compression algorithm is
  `zstd` (stored as `Compression::Zstd`). When compressed, `stored_size` is the
  compressed size and `raw_size` is the original uncompressed size.
- Readers use `compression` to choose the decompressor. If decompression fails,
  the reader returns `AssetError::DecompressFailed`.

## Endianness & portability

- All integer fields are written little-endian. Readers on big-endian platforms
  must convert accordingly (the included implementation assumes little-endian
  hosts; if you need big-endian support add conversion helpers).
- The current v1 reader requires **exactly** 64-byte header and TOC entries.
  `toc_entry_size` is validated, not used to skip unknown trailing fields.

## Versioning

- The current reader requires `format_version == 1`. Any unknown version,
  header size or TOC-entry size is rejected, not migrated or ignored.
- Any incompatible field change needs a **new format version** and an explicit
  matching reader/writer compatibility decision and tests. Simply appending
  fields or enlarging the structs is not accepted by today's reader.

## Example layout

[Header][BlobA][BlobB][BlobC][TOC-entry-A][TOC-entry-B][TOC-entry-C]

- The header points to the TOC offset; each TOC entry points back to the blob
  data offset, sizes, and content hash.

## Runtime API (summary)

- `std::unique_ptr<asset::PakMount> asset::PakMount::Open(const std::string& path, AssetError& out_err, bool verify_toc_hash)`
  - Returns a `unique_ptr` on success, or `nullptr` and sets `out_err` on failure.
- `bool PakMount::Has(AssetId id) const` — quick membership check.
- `std::optional<AssetInfo> PakMount::GetInfo(AssetId id) const` — metadata only.
- `std::optional<AssetBlob> PakMount::Load(AssetId id, AssetError& out_err, bool verify_content_hash) const` — load and optionally verify content hash.

See `include/asset_runtime.h` for exact types and `src/pak_reader.cpp` /
`src/pak_writer.cpp` for reference implementations.

## Notes and recommendations

- Writer should compute and store canonical keys and use `XxHash64` consistently.
- The current writer **always records** `toc_hash`, including for empty TOCs.
  The reader verifies it by default and can disable just the digest check via
  `PakMount::Open(..., verify_toc_hash=false)`; mandatory structural checks
  still apply.
- Keep `format_version` increments minimal and append-only for structs where
  possible to remain forwards-compatible.

