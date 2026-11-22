#!/usr/bin/env python3
"""Deterministically pack reviewed PNG sprites/strips into a texture atlas.

This optional authoring tool needs Pillow. Neither assetc nor asset_runtime use it.
"""

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import sys
import tempfile

from assetprep import AssetPrepError, relative_path as validated_relative_path

try:
    from PIL import Image, UnidentifiedImageError
except ImportError:
    print("atlasprep: Pillow is required (pip install Pillow)", file=sys.stderr)
    raise SystemExit(2)

Image.MAX_IMAGE_PIXELS = 64 * 1024 * 1024
MAX_SOURCE_BYTES = 64 * 1024 * 1024
MAX_ATLAS_DIMENSION = 8192
MAX_FRAMES = 2048


class AtlasError(ValueError):
    """Invalid source, layout or output specification."""


def logical_path(value):
    # Use the same safety contract as assetprep's canonical asset keys.
    try:
        return validated_relative_path(value)
    except AssetPrepError as exc:
        raise AtlasError(f"unsafe asset key: {value!r}") from exc


def integer(value, name, minimum=1, maximum=MAX_ATLAS_DIMENSION):
    if type(value) is not int or value < minimum or value > maximum:
        raise AtlasError(f"{name} must be an integer in [{minimum},{maximum}]")
    return value


def load_layout(layout_path):
    try:
        layout = json.loads(layout_path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        raise AtlasError(f"cannot read atlas layout: {exc}") from exc
    if not isinstance(layout, dict) or layout.get("version") != 1:
        raise AtlasError("layout version must be 1")
    keys = [layout.get("atlas_key"), layout.get("metadata_key")]
    for key in keys:
        logical_path(key)
    if keys[0].casefold() == keys[1].casefold():
        raise AtlasError("atlas and metadata keys must be different")
    if not keys[0].lower().endswith(".png") or not keys[1].lower().endswith(".json"):
        raise AtlasError("atlas_key must end in .png and metadata_key in .json")
    width = integer(layout.get("atlas_width"), "atlas_width")
    max_height = integer(layout.get("max_height", MAX_ATLAS_DIMENSION), "max_height")
    padding = integer(layout.get("padding", 1), "padding", 0, 16)
    entries = layout.get("sprites")
    if not isinstance(entries, list) or not entries:
        raise AtlasError("sprites must be a nonempty list")
    ids = set()
    specs = []
    total_frames = 0
    for item in entries:
        if not isinstance(item, dict):
            raise AtlasError("sprite entry must be an object")
        name = item.get("id")
        if not isinstance(name, str) or not name.strip() or len(name) > 128 or any(ord(c) < 32 for c in name):
            raise AtlasError("sprite id must be nonempty printable text <=128 chars")
        if name.casefold() in ids:
            raise AtlasError(f"duplicate sprite id: {name}")
        ids.add(name.casefold())
        source = item.get("source_key")
        logical_path(source)
        if not source.lower().endswith(".png"):
            raise AtlasError("sprite source_key must be PNG")
        frame_w = integer(item.get("frame_width"), "frame_width")
        frame_h = integer(item.get("frame_height"), "frame_height")
        count = integer(item.get("frame_count", 1), "frame_count", 1, MAX_FRAMES)
        direction = item.get("direction", "horizontal")
        if direction not in ("horizontal", "vertical"):
            raise AtlasError("direction must be horizontal or vertical")
        pivot = item.get("pivot", [frame_w // 2, frame_h // 2])
        if (not isinstance(pivot, list) or len(pivot) != 2 or
                any(type(value) is not int for value in pivot) or
                not (0 <= pivot[0] <= frame_w and 0 <= pivot[1] <= frame_h)):
            raise AtlasError("pivot must be two integral coordinates within the frame")
        output_w = integer(item.get("output_width", frame_w), "output_width")
        output_h = integer(item.get("output_height", frame_h), "output_height")
        resize_filter = item.get("resize_filter", "lanczos")
        if resize_filter not in ("lanczos", "nearest"):
            raise AtlasError("resize_filter must be lanczos or nearest")
        if output_w + 2 * padding > width or output_h + 2 * padding > max_height:
            raise AtlasError(f"{name}: padded frame too large for atlas")
        total_frames += count
        if total_frames > MAX_FRAMES:
            raise AtlasError("too many atlas frames")
        specs.append((name, source, frame_w, frame_h, count, direction, pivot,
                      output_w, output_h, resize_filter))
    return keys, width, max_height, padding, sorted(specs, key=lambda spec: spec[0])


def validate_stage(source_dir, out_dir):
    if source_dir.is_symlink() or out_dir.is_symlink():
        raise AtlasError("source and output directories cannot be symlinks")
    source_dir = source_dir.resolve()
    out_dir = out_dir.resolve()
    if not source_dir.is_dir() or source_dir == out_dir or source_dir.is_relative_to(out_dir) or out_dir.is_relative_to(source_dir):
        raise AtlasError("source and output directories must exist separately, without nesting")
    if out_dir.exists() and not out_dir.is_dir():
        raise AtlasError("existing output must be a directory")
    for path in source_dir.rglob("*"):
        if path.is_symlink():
            raise AtlasError(f"symlinks are forbidden in staged source tree: {path}")
    return source_dir, out_dir


def source_bytes(root, key):
    path = root / logical_path(key)
    if not path.is_file() or path.is_symlink() or not path.resolve().is_relative_to(root):
        raise AtlasError(f"source PNG missing or unsafe: {key}")
    if path.stat().st_size > MAX_SOURCE_BYTES:
        raise AtlasError(f"source PNG too large: {key}")
    return path.read_bytes()


def unpack_frames(root, specs):
    from io import BytesIO
    frames = []
    for name, source, fw, fh, count, direction, pivot, out_w, out_h, resize_filter in specs:
        data = source_bytes(root, source)
        try:
            with Image.open(BytesIO(data)) as image:
                if image.format != "PNG" or getattr(image, "n_frames", 1) != 1:
                    raise AtlasError(f"{source}: only static PNG sources are supported")
                expected = (fw * count, fh) if direction == "horizontal" else (fw, fh * count)
                if image.size != expected:
                    raise AtlasError(f"{source}: expected sheet dimensions {expected}, got {image.size}")
                rgba = image.convert("RGBA")
                rgba.load()
        except (OSError, ValueError, UnidentifiedImageError) as exc:
            if isinstance(exc, AtlasError):
                raise
            raise AtlasError(f"{source}: cannot decode PNG: {exc}") from exc
        digest = hashlib.sha256(data).hexdigest()
        for index in range(count):
            left, top = (index * fw, 0) if direction == "horizontal" else (0, index * fh)
            cropped = rgba.crop((left, top, left + fw, top + fh))
            if (out_w, out_h) != (fw, fh):
                method = (Image.Resampling.LANCZOS if resize_filter == "lanczos"
                          else Image.Resampling.NEAREST)
                cropped = cropped.resize((out_w, out_h), method)
            # Half-up integer pixel scaling (independent of locale/float rounding).
            final_pivot = [(pivot[0] * out_w + fw // 2) // fw,
                           (pivot[1] * out_h + fh // 2) // fh]
            frames.append((name, index, source, digest, cropped, out_w, out_h,
                           final_pivot, fw, fh, resize_filter))
    return frames


def pack_positions(frames, width, max_height, pad):
    positions = []
    x = y = row_height = 0
    for name, index, source, digest, frame, fw, fh, pivot, source_w, source_h, resize_filter in frames:
        footprint_w, footprint_h = fw + pad * 2, fh + pad * 2
        if x + footprint_w > width:
            x, y, row_height = 0, y + row_height, 0
        if y + footprint_h > max_height:
            raise AtlasError(f"atlas exceeds max_height at {name} frame {index}")
        positions.append((x + pad, y + pad))
        x += footprint_w
        row_height = max(row_height, footprint_h)
    return positions, y + row_height


def paste_extruded(atlas, frame, x, y, padding):
    atlas.paste(frame, (x, y))
    if not padding:
        return
    w, h = frame.size
    nearest = Image.Resampling.NEAREST
    atlas.paste(frame.crop((0, 0, 1, h)).resize((padding, h), nearest), (x - padding, y))
    atlas.paste(frame.crop((w - 1, 0, w, h)).resize((padding, h), nearest), (x + w, y))
    atlas.paste(frame.crop((0, 0, w, 1)).resize((w, padding), nearest), (x, y - padding))
    atlas.paste(frame.crop((0, h - 1, w, h)).resize((w, padding), nearest), (x, y + h))
    for xx, yy, color in ((x - padding, y - padding, frame.getpixel((0, 0))),
                          (x + w, y - padding, frame.getpixel((w - 1, 0))),
                          (x - padding, y + h, frame.getpixel((0, h - 1))),
                          (x + w, y + h, frame.getpixel((w - 1, h - 1)))):
        atlas.paste(Image.new("RGBA", (padding, padding), color), (xx, yy))


def compile_atlas(source_dir, layout_path, out_dir, replace=False):
    source_dir, out_dir = validate_stage(source_dir, out_dir)
    if out_dir.exists() and not replace:
        raise AtlasError("output already exists; use --replace")
    keys, width, max_height, padding, specs = load_layout(layout_path)
    current_names = {p.relative_to(source_dir).as_posix().casefold() for p in source_dir.rglob("*") if p.is_file()}
    for key in keys:
        if key.casefold() in current_names:
            raise AtlasError(f"generated key collides with approved source: {key}")
    frames = unpack_frames(source_dir, specs)
    positions, height = pack_positions(frames, width, max_height, padding)
    atlas = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    metadata = {"version": 1, "atlas_key": keys[0], "width": width, "height": height,
                "padding": padding, "frames": []}
    for (name, index, source, digest, image, fw, fh, pivot, source_w, source_h, resize_filter), (x, y) in zip(frames, positions):
        paste_extruded(atlas, image, x, y, padding)
        metadata["frames"].append({
            "id": name, "frame": index, "x": x, "y": y,
            "width": fw, "height": fh, "pivot": pivot,
            "source_frame_width": source_w, "source_frame_height": source_h,
            "resize_filter": resize_filter,
            "source_key": source, "source_sha256": digest,
        })

    out_dir.parent.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix=f".{out_dir.name}-atlas-", dir=out_dir.parent))
    backup = None
    try:
        shutil.copytree(source_dir, temporary, dirs_exist_ok=True)
        atlas_file = temporary / logical_path(keys[0])
        atlas_file.parent.mkdir(parents=True, exist_ok=True)
        atlas.save(atlas_file, format="PNG", optimize=False, compress_level=9)
        metadata_file = temporary / logical_path(keys[1])
        metadata_file.parent.mkdir(parents=True, exist_ok=True)
        metadata_file.write_text(json.dumps(metadata, sort_keys=True, indent=2) + "\n", encoding="utf-8")
        if out_dir.exists():
            backup = Path(tempfile.mkdtemp(prefix=f".{out_dir.name}-backup-", dir=out_dir.parent))
            backup.rmdir()
            os.replace(out_dir, backup)
        try:
            os.replace(temporary, out_dir)
            temporary = None
        except OSError:
            if backup:
                os.replace(backup, out_dir)
                backup = None
            raise
        if backup:
            shutil.rmtree(backup, ignore_errors=True)
        return len(frames)
    finally:
        if temporary and temporary.exists():
            shutil.rmtree(temporary, ignore_errors=True)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("approved_dir", type=Path)
    parser.add_argument("layout", type=Path)
    parser.add_argument("out_dir", type=Path)
    parser.add_argument("--replace", action="store_true")
    args = parser.parse_args(argv)
    try:
        count = compile_atlas(args.approved_dir, args.layout, args.out_dir, args.replace)
        print(f"Packed {count} sprite frame(s) into {args.out_dir}")
        return 0
    except (AtlasError, OSError, Image.DecompressionBombError) as exc:
        print(f"atlasprep: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
