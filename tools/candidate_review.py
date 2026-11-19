#!/usr/bin/env python3
"""Make a deterministic review-only contact sheet from quarantined PNG candidates.

Requires Pillow during authoring; never promotes candidates to approved assets.
"""

import argparse
import hashlib
from io import BytesIO
import json
import math
import os
from pathlib import Path
import re
import shutil
import sys
import tempfile

try:
    from PIL import Image, ImageDraw, UnidentifiedImageError
except ImportError:
    print("candidate_review: Pillow is required (pip install Pillow)", file=sys.stderr)
    raise SystemExit(2)

from assetgenerate import QUARANTINE_MARKER, MAX_CANDIDATES
from assetprep import AssetPrepError, inspect_png

Image.MAX_IMAGE_PIXELS = 64 * 1024 * 1024
MAX_CANDIDATE_BYTES = 64 * 1024 * 1024
MAX_RECEIPT_BYTES = 128 * 1024
MAX_DIMENSION = 8192


class ReviewError(ValueError):
    """A candidate batch is untrusted, modified, or too large."""


def read_candidates(candidate_dir):
    marker = candidate_dir / QUARANTINE_MARKER
    if marker.is_symlink() or not marker.is_file() or marker.stat().st_size > MAX_RECEIPT_BYTES:
        raise ReviewError("candidate directory lacks a valid quarantine receipt")
    raw = marker.read_bytes()
    if len(raw) > MAX_RECEIPT_BYTES:
        raise ReviewError("quarantine receipt is too large")
    try:
        receipt = json.loads(raw)
    except (ValueError, UnicodeDecodeError) as exc:
        raise ReviewError(f"invalid quarantine receipt: {exc}") from exc
    if (not isinstance(receipt, dict) or type(receipt.get("version")) is not int or
            receipt["version"] != 1 or receipt.get("state") != "unreviewed" or
            receipt.get("approved") is not False or receipt.get("eligible_for_packaging") is not False):
        raise ReviewError("not an unreviewed candidate receipt")
    entries = receipt.get("candidates")
    if not isinstance(entries, list) or not 1 <= len(entries) <= MAX_CANDIDATES:
        raise ReviewError("invalid candidate list")
    if type(receipt.get("candidate_count")) is not int or receipt["candidate_count"] != len(entries):
        raise ReviewError("candidate count does not match receipt")
    job_id = receipt.get("job_id")
    if not isinstance(job_id, str) or not re.fullmatch(r"[a-z0-9][a-z0-9_-]{0,63}", job_id):
        raise ReviewError("invalid job identifier")
    observed = set()
    frames = []
    for index, entry in enumerate(entries, 1):
        if not isinstance(entry, dict):
            raise ReviewError("candidate entry is not an object")
        name = entry.get("file")
        if name != f"candidate-{index:03d}.png":
            raise ReviewError("candidate filenames must be canonical and ordered")
        file_path = candidate_dir / name
        if file_path.is_symlink() or not file_path.is_file() or not file_path.resolve().is_relative_to(candidate_dir):
            raise ReviewError(f"candidate file missing or unsafe: {name}")
        if file_path.stat().st_size > MAX_CANDIDATE_BYTES:
            raise ReviewError(f"candidate too large: {name}")
        data = file_path.read_bytes()
        if len(data) > MAX_CANDIDATE_BYTES:
            raise ReviewError(f"candidate too large: {name}")
        digest = hashlib.sha256(data).hexdigest()
        if entry.get("sha256") != digest or type(entry.get("bytes")) is not int or entry["bytes"] != len(data):
            raise ReviewError(f"candidate image changed since generation: {name}")
        try:
            details = inspect_png(data)
        except AssetPrepError as exc:
            raise ReviewError(f"candidate PNG invalid: {name}: {exc}") from exc
        expected_size = (entry.get("width"), entry.get("height"))
        if (details["width"], details["height"]) != expected_size:
            raise ReviewError(f"candidate dimensions changed: {name}")
        if details["width"] > MAX_DIMENSION or details["height"] > MAX_DIMENSION:
            raise ReviewError("candidate dimensions exceed review limit")
        try:
            with Image.open(BytesIO(data)) as img:
                if img.format != "PNG" or getattr(img, "n_frames", 1) != 1:
                    raise ReviewError(f"expected static PNG: {name}")
                rgba = img.convert("RGBA")
                rgba.load()
        except (OSError, ValueError, UnidentifiedImageError, Image.DecompressionBombError) as exc:
            if isinstance(exc, ReviewError):
                raise
            raise ReviewError(f"cannot decode candidate: {name}: {exc}") from exc
        observed.add(name)
        frames.append((name, digest, rgba))

    actual_pngs = {p.name for p in candidate_dir.glob("*.png")}
    if observed != actual_pngs:
        raise ReviewError("candidate directory contains unlisted PNG files")
    return receipt, hashlib.sha256(raw).hexdigest(), frames


def create_sheet(job_id, frames, columns, tile_size):
    rows = math.ceil(len(frames) / columns)
    margin, gap, title_h, label_h = 16, 14, 42, 30
    width = margin * 2 + columns * tile_size + (columns - 1) * gap
    height = title_h + margin + rows * (tile_size + label_h) + (rows - 1) * gap + margin
    sheet = Image.new("RGB", (width, height), (238, 238, 238))
    draw = ImageDraw.Draw(sheet)
    draw.text((margin, 16), f"UNREVIEWED  /  {job_id}  /  {len(frames)} candidates",
              fill=(35, 35, 35))
    for i, (name, digest, img) in enumerate(frames):
        x = margin + (i % columns) * (tile_size + gap)
        y = title_h + margin + (i // columns) * (tile_size + label_h + gap)
        # A neutral chessboard background makes accidental missing alpha obvious.
        square = 12
        for yy in range(y, y + tile_size, square):
            for xx in range(x, x + tile_size, square):
                shade = 216 if (((xx - x) // square + (yy - y) // square) % 2) else 244
                draw.rectangle((xx, yy, min(xx + square - 1, x + tile_size - 1),
                                min(yy + square - 1, y + tile_size - 1)),
                               fill=(shade, shade, shade))
        scale = min(tile_size / img.width, tile_size / img.height)
        size = (max(1, round(img.width * scale)), max(1, round(img.height * scale)))
        method = Image.Resampling.LANCZOS if scale < 1 else Image.Resampling.NEAREST
        preview = img.resize(size, resample=method)
        image_x = x + (tile_size - size[0]) // 2
        image_y = y + (tile_size - size[1]) // 2
        sheet.paste(preview, (image_x, image_y), preview)
        draw.text((x, y + tile_size + 7), f"{name}  sha:{digest[:10]}", fill=(28, 28, 28))
    return sheet


def review(candidate_dir, out_dir, *, columns=3, tile_size=192, replace=False):
    if candidate_dir.is_symlink() or out_dir.is_symlink():
        raise ReviewError("candidate and review directories cannot be symlinks")
    candidate_dir = candidate_dir.resolve()
    out_dir = out_dir.resolve()
    if (not candidate_dir.is_dir() or candidate_dir == out_dir or
            candidate_dir.is_relative_to(out_dir) or out_dir.is_relative_to(candidate_dir)):
        raise ReviewError("candidate and review directories must not overlap")
    if out_dir.exists() and (not out_dir.is_dir() or not replace):
        raise ReviewError("review output exists; pass --replace to rebuild")
    if type(columns) is not int or not 1 <= columns <= 4:
        raise ReviewError("columns must be 1–4")
    if type(tile_size) is not int or not 96 <= tile_size <= 384:
        raise ReviewError("tile-size must be 96–384")
    receipt, receipt_hash, frames = read_candidates(candidate_dir)
    sheet = create_sheet(receipt["job_id"], frames, columns, tile_size)
    metadata = {
        "version": 1,
        "kind": "candidate-contact-sheet",
        "state": "unreviewed",
        "approved": False,
        "eligible_for_packaging": False,
        "job_id": receipt["job_id"],
        "source_receipt_sha256": receipt_hash,
        "preview_file": "contact-sheet.png",
        "candidates": [{"file": name, "sha256": digest, "width": img.width, "height": img.height}
                       for name, digest, img in frames],
    }

    out_dir.parent.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix=f".{out_dir.name}-review-", dir=out_dir.parent))
    backup = None
    try:
        sheet.save(temporary / "contact-sheet.png", format="PNG", compress_level=9, optimize=False)
        (temporary / QUARANTINE_MARKER).write_text(
            json.dumps(metadata, sort_keys=True, indent=2) + "\n", encoding="utf-8"
        )
        if out_dir.exists():
            backup = Path(tempfile.mkdtemp(prefix=f".{out_dir.name}-backup-", dir=out_dir.parent))
            backup.rmdir()
            os.replace(out_dir, backup)
        try:
            os.replace(temporary, out_dir)
            temporary = None
        except OSError:
            if backup is not None:
                os.replace(backup, out_dir)
                backup = None
            raise
        if backup is not None:
            shutil.rmtree(backup, ignore_errors=True)
    finally:
        if temporary is not None and temporary.exists():
            shutil.rmtree(temporary, ignore_errors=True)
    return len(frames)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("candidate_dir", type=Path)
    parser.add_argument("out_dir", type=Path)
    parser.add_argument("--columns", type=int, default=3)
    parser.add_argument("--tile-size", type=int, default=192)
    parser.add_argument("--replace", action="store_true")
    args = parser.parse_args(argv)
    try:
        count = review(args.candidate_dir, args.out_dir,
                       columns=args.columns, tile_size=args.tile_size, replace=args.replace)
        print(f"Prepared review-only contact sheet for {count} candidate(s) in {args.out_dir}")
        return 0
    except (ReviewError, OSError, Image.DecompressionBombError) as exc:
        print(f"candidate_review: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
