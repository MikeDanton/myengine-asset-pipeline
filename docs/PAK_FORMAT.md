# .pak Wire Format

This document describes the on-disk `.pak` wire format used by the asset pipeline.
It is intentionally simple and versioned to allow forward/backward compatibility.

## High-level layout

File layout (all multi-byte integer fields are little-endian):

- PakHeader (fixed-size header)
- Blob data (one or more asset blobs, stored back-to-back)
- TOC (table-of-contents) raw bytes

The header contains offsets and sizes that point to the TOC and version info.

## PakHeader (C representation)

See `include/pak_format.h` for the precise C++ struct. Key fields:

- `magic[8]` — ASCII magic (`"PACKPIPE"` or similar) to identify the file.
- `format_version` (uint32) — format version. Readers must check this.
- `header_size` (uint32) — size of the header in bytes (allows future header growth).
- `toc_offset` (uint64) — file offset where the TOC begins (0 if no TOC).
- `toc_count` (uint32) — number of TOC entries.
- `toc_entry_size` (uint32) — size of a TOC entry on-disk (useful for forward compatibility).
- `toc_hash` (16 bytes) — BLAKE3-128 of the raw TOC bytes (zero when not present).

All fields are stored in little-endian.

## TOC entry (PakTocEntryWire)

Each TOC entry is a fixed-size, packed struct with the following important fields:

- `id` (uint64) — AssetId computed as `XxHash64(canonical_key)`.
- `type` (uint16) — Asset type identifier (project-defined enum).
- `type_version` (uint16) — Version of the type layout.
- `flags` (uint16) — Bitflags for asset-specific properties.
- `compression` (uint8) — `0 = None`, `1 = Zstd` (other values reserved).
- `reserved` / padding to align fields as defined in `pak_format.h`.
- `data_offset` (uint64) — offset to the stored bytes for this asset.
- `raw_size` (uint64) — uncompressed size (0 for empty asset).
- `stored_size` (uint64) — stored size (after compression if compressed).
- `content_hash` (16 bytes) — BLAKE3-128 of the raw (uncompressed) bytes.

TOC entries are serialized as raw bytes using the platform packing defined in
`pak_format.h`. The reader recomputes the `toc_hash` over the concatenated raw
TOC entry bytes and compares to `header.toc_hash` when `verify_toc_hash` is requested.

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
- TOC layout is fixed per `format_version`. `toc_entry_size` allows readers to
  skip unknown trailing bytes in each entry (forward-compatibility).

## Versioning

- `format_version` in the header indicates the wire-format version. The reader
  should fail if the version is unknown/unsupported.
- New fields should be appended at the end of structs and `header_size`/
  `toc_entry_size` updated accordingly to preserve compatibility.

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
- Writers should optionally write `toc_hash` and readers should verify it when
  requested to protect against TOC corruption.
- Keep `format_version` increments minimal and append-only for structs where
  possible to remain forwards-compatible.

If you want, I can also generate a small binary inspector utility that prints
out header and TOC contents for debugging `.pak` files.
