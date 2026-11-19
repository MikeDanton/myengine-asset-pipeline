#!/usr/bin/env python3
"""Stage reviewed human/AI PNG files for the existing assetc packer (stdlib only)."""

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import struct
import sys
import tempfile
import zlib

MAX_PNG_BYTES = 64 * 1024 * 1024
MAX_DIMENSION = 16384
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


class AssetPrepError(ValueError):
    """Invalid inputs or unsafe staging request."""


def require_text(obj, name):
    value = obj.get(name)
    if not isinstance(value, str) or not value.strip():
        raise AssetPrepError(f"{name} must be nonempty text")
    return value


def relative_path(value):
    if not isinstance(value, str) or not value or "\\" in value or ":" in value:
        raise AssetPrepError("paths must be relative POSIX paths")
    parts = value.split("/")
    if any(part in ("", ".", "..") or any(ord(c) < 32 for c in part) for part in parts):
        raise AssetPrepError(f"invalid path: {value!r}")
    if PurePosixPath(value).is_absolute():
        raise AssetPrepError(f"absolute path forbidden: {value!r}")
    return Path(*parts)


def load_source(root, name, out_dir):
    relative = relative_path(name)
    source = root / relative
    resolved = source.resolve()
    if not resolved.is_relative_to(root) or resolved.is_relative_to(out_dir):
        raise AssetPrepError(f"source outside manifest root or inside staging: {name}")
    if not resolved.is_file():
        raise AssetPrepError(f"source file missing: {name}")
    if source.stat().st_size > MAX_PNG_BYTES:
        raise AssetPrepError(f"source exceeds {MAX_PNG_BYTES} bytes: {name}")
    content = source.read_bytes()
    if len(content) > MAX_PNG_BYTES:
        raise AssetPrepError(f"source exceeds {MAX_PNG_BYTES} bytes: {name}")
    return content


class HumanFileAdapter:
    """Artist-supplied files require attribution and a declared license."""

    @staticmethod
    def provenance(source):
        return {
            "adapter": "human-file",
            "file": require_text(source, "file"),
            "creator": require_text(source, "creator"),
            "license": require_text(source, "license"),
        }


class AIImportAdapter:
    """Already-generated files; generation providers are deliberately outside v1."""

    @staticmethod
    def provenance(source):
        if source.get("rights_reviewed") is not True:
            raise AssetPrepError("AI assets require explicit rights_reviewed: true")
        prompt_hash = require_text(source, "prompt_sha256")
        if len(prompt_hash) != 64 or any(c not in "0123456789abcdef" for c in prompt_hash):
            raise AssetPrepError("prompt_sha256 must contain 64 lowercase hex characters")
        return {
            "adapter": "ai-import",
            "file": require_text(source, "file"),
            "provider": require_text(source, "provider"),
            "model": require_text(source, "model"),
            "prompt_sha256": prompt_hash,
            "rights_note": require_text(source, "rights_note"),
            "rights_reviewed": True,
            "reviewed_by": require_text(source, "reviewed_by"),
        }


SOURCE_ADAPTERS = {"human-file": HumanFileAdapter(), "ai-import": AIImportAdapter()}


def inspect_png(data):
    """Check PNG chunk structure/CRC (not pixel decoding or animation correctness)."""
    if not data.startswith(PNG_SIGNATURE):
        raise AssetPrepError("not a PNG file")
    pos = len(PNG_SIGNATURE)
    width = height = color = None
    seen_idat = seen_iend = has_trns = False
    while pos + 12 <= len(data):
        size = struct.unpack_from(">I", data, pos)[0]
        end = pos + 12 + size
        if end > len(data):
            raise AssetPrepError("truncated PNG chunk")
        kind = data[pos + 4:pos + 8]
        payload = data[pos + 8:pos + 8 + size]
        crc = struct.unpack_from(">I", data, pos + 8 + size)[0]
        if zlib.crc32(kind + payload) != crc:
            raise AssetPrepError("PNG chunk CRC mismatch")
        if width is None:
            if kind != b"IHDR" or size != 13:
                raise AssetPrepError("PNG must start with 13-byte IHDR")
            width, height, depth, color, compression, filtering, interlace = struct.unpack(">IIBBBBB", payload)
            valid_depths = {0: (1, 2, 4, 8, 16), 2: (8, 16), 3: (1, 2, 4, 8), 4: (8, 16), 6: (8, 16)}
            if (not width or not height or width > MAX_DIMENSION or height > MAX_DIMENSION
                    or depth not in valid_depths.get(color, ()) or compression or filtering
                    or interlace not in (0, 1)):
                raise AssetPrepError("unsupported PNG IHDR")
        elif kind == b"IHDR":
            raise AssetPrepError("duplicate IHDR")
        elif kind == b"IDAT":
            seen_idat = True
        elif kind == b"tRNS":
            has_trns = True
        elif kind == b"IEND":
            if size != 0 or end != len(data) or not seen_idat:
                raise AssetPrepError("invalid PNG end")
            seen_iend = True
            break
        pos = end
    if not seen_iend:
        raise AssetPrepError("PNG missing IEND")
    return {"width": width, "height": height, "alpha_capable": color in (4, 6) or has_trns}


def validate_expected(image, details):
    if image is None:
        return
    if not isinstance(image, dict):
        raise AssetPrepError("image must be an object")
    for dimension in ("width", "height"):
        if dimension in image:
            if type(image[dimension]) is not int or image[dimension] != details[dimension]:
                raise AssetPrepError(f"PNG {dimension} does not match manifest")
    if "alpha" in image:
        if type(image["alpha"]) is not bool or (image["alpha"] and not details["alpha_capable"]):
            raise AssetPrepError("PNG alpha requirement not met")


def collect(manifest_path, out_dir, provenance_path):
    root = manifest_path.parent.resolve()
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        raise AssetPrepError(f"cannot read manifest: {exc}") from exc
    if not isinstance(manifest, dict) or type(manifest.get("version")) is not int or manifest["version"] != 1:
        raise AssetPrepError("manifest version must be 1")
    assets = manifest.get("assets")
    if not isinstance(assets, list) or not assets:
        raise AssetPrepError("assets must be a nonempty list")
    seen = set()
    staged = []
    for i, item in enumerate(assets):
        if not isinstance(item, dict):
            raise AssetPrepError(f"asset #{i} must be an object")
        key = require_text(item, "key")
        relative_path(key)
        if not key.lower().endswith(".png") or item.get("kind") != "png":
            raise AssetPrepError(f"{key}: v1 only accepts PNG assets")
        if key.casefold() in seen:
            raise AssetPrepError(f"duplicate or case-colliding asset key: {key}")
        seen.add(key.casefold())
        if item.get("approved") is not True:
            raise AssetPrepError(f"{key}: asset must be explicitly approved")
        source = item.get("source")
        if not isinstance(source, dict):
            raise AssetPrepError(f"{key}: source must be an object")
        adapter_name = source.get("adapter")
        adapter = SOURCE_ADAPTERS.get(adapter_name) if isinstance(adapter_name, str) else None
        if adapter is None:
            raise AssetPrepError(f"{key}: unknown source adapter")
        origin = adapter.provenance(source)
        path = (root / relative_path(origin["file"])).resolve()
        if path == manifest_path or path == provenance_path:
            raise AssetPrepError("source cannot be the manifest or provenance report")
        data = load_source(root, origin["file"], out_dir)
        details = inspect_png(data)
        validate_expected(item.get("image"), details)
        staged.append((key, data, {"key": key, "kind": "png", "sha256": hashlib.sha256(data).hexdigest(),
                                   "size": len(data), "image": details, "source": origin}))
    staged.sort(key=lambda entry: entry[0])
    return staged


def stage(manifest_path, out_dir, provenance_path, replace=False):
    if out_dir.is_symlink() or provenance_path.is_symlink():
        raise AssetPrepError("output paths cannot be symlinks")
    manifest_path = manifest_path.resolve()
    out_dir = out_dir.resolve()
    provenance_path = provenance_path.resolve()
    if manifest_path.is_relative_to(out_dir):
        raise AssetPrepError("stage output cannot contain the manifest")
    if out_dir.exists() and not replace:
        raise AssetPrepError("stage output exists; pass --replace to rebuild")
    if provenance_path == manifest_path:
        raise AssetPrepError("provenance report cannot overwrite the manifest")
    if out_dir == provenance_path or provenance_path.is_relative_to(out_dir):
        raise AssetPrepError("provenance report must be outside staged assets (never packed)")
    if out_dir.exists() and (not out_dir.is_dir() or out_dir.is_symlink()):
        raise AssetPrepError("stage output must be a directory, not a symlink")
    if provenance_path.is_dir() or provenance_path.is_symlink():
        raise AssetPrepError("provenance target must be a regular file path")
    staged = collect(manifest_path, out_dir, provenance_path)
    out_dir.parent.mkdir(parents=True, exist_ok=True)
    provenance_path.parent.mkdir(parents=True, exist_ok=True)
    receipt = {"version": 1, "assets": [entry[2] for entry in staged]}
    temporary = Path(tempfile.mkdtemp(prefix=f".{out_dir.name}-stage-", dir=out_dir.parent))
    report_tmp = None
    backup = None
    try:
        for key, data, _ in staged:
            dst = temporary / relative_path(key)
            dst.parent.mkdir(parents=True, exist_ok=True)
            dst.write_bytes(data)
        with tempfile.NamedTemporaryFile("w", encoding="utf-8", prefix=".assetprep-", suffix=".json",
                                         dir=provenance_path.parent, delete=False) as report:
            report_tmp = Path(report.name)
            json.dump(receipt, report, indent=2, sort_keys=True)
            report.write("\n")
        if out_dir.exists():
            backup = Path(tempfile.mkdtemp(prefix=f".{out_dir.name}-backup-", dir=out_dir.parent))
            backup.rmdir()
            os.replace(out_dir, backup)
        try:
            os.replace(temporary, out_dir)
            temporary = None
            os.replace(report_tmp, provenance_path)
            report_tmp = None
        except OSError:
            if out_dir.exists():
                shutil.rmtree(out_dir)
            if backup:
                os.replace(backup, out_dir)
                backup = None
            raise
        if backup:
            shutil.rmtree(backup)
        return len(staged)
    finally:
        if temporary is not None and temporary.exists():
            shutil.rmtree(temporary)
        if report_tmp is not None:
            report_tmp.unlink(missing_ok=True)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path, help="JSON manifest with reviewed PNG sources")
    parser.add_argument("out_dir", type=Path, help="directory for assetc inputs")
    parser.add_argument("--provenance", required=True, type=Path,
                        help="deterministic sidecar JSON outside out_dir; NOT shipped in pak")
    parser.add_argument("--replace", action="store_true", help="replace existing staged directory")
    args = parser.parse_args(argv)
    try:
        count = stage(args.manifest, args.out_dir, args.provenance, replace=args.replace)
        print(f"Staged {count} reviewed PNG asset(s) in {args.out_dir}")
        return 0
    except (AssetPrepError, OSError) as exc:
        print(f"assetprep: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
