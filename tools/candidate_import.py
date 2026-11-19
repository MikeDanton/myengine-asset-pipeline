#!/usr/bin/env python3
"""Quarantine externally generated PNG candidates (ChatGPT, local models, etc.).

Standard library only. Never approves content, contacts a provider, or writes
into assetprep/atlasprep's approved staging tree.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import sys
import tempfile

from assetprep import AssetPrepError, inspect_png

QUARANTINE_MARKER = ".asset-pipeline-quarantine.json"
MAX_SPEC_BYTES = 65536
MAX_IMAGE_BYTES = 32 * 1024 * 1024
MAX_DIMENSION = 8192
MAX_CANDIDATES = 16


class ImportError_(ValueError):
    """Invalid external artwork or unsafe import destination."""


def relative_path(value):
    if (not isinstance(value, str) or not value or len(value) > 512 or
            value.startswith("/") or "\\" in value or ":" in value or
            any(ord(ch) < 32 for ch in value) or
            any(part in ("", ".", "..") for part in value.split("/"))):
        raise ImportError_(f"invalid candidate source path: {value!r}")
    return Path(*value.split("/"))


def required_text(value, label):
    if (not isinstance(value, str) or not value.strip() or len(value) > 128 or
            any(ord(ch) < 32 for ch in value)):
        raise ImportError_(f"{label} must be printable text (1–128 chars)")
    return value


def read_spec(spec_path):
    if not spec_path.is_file() or spec_path.stat().st_size > MAX_SPEC_BYTES:
        raise ImportError_("import spec missing or exceeds size limit")
    data = spec_path.read_bytes()
    if len(data) > MAX_SPEC_BYTES:
        raise ImportError_("import spec exceeds size limit")
    try:
        manifest = json.loads(data)
    except (UnicodeDecodeError, ValueError) as exc:
        raise ImportError_(f"invalid import JSON: {exc}") from exc
    if not isinstance(manifest, dict) or type(manifest.get("version")) is not int or manifest["version"] != 1:
        raise ImportError_("import spec version must be 1")
    allowed = {"version", "job_id", "provider", "model", "prompt_sha256", "candidates"}
    if set(manifest) - allowed:
        raise ImportError_("unknown import spec fields")
    job_id = manifest.get("job_id")
    if not isinstance(job_id, str) or not re.fullmatch(r"[a-z0-9][a-z0-9_-]{0,63}", job_id):
        raise ImportError_("job_id must be a portable lowercase slug")
    provider = required_text(manifest.get("provider"), "provider")
    model = required_text(manifest.get("model"), "model")
    prompt_digest = manifest.get("prompt_sha256")
    if (not isinstance(prompt_digest, str) or
            not re.fullmatch("[0-9a-f]{64}", prompt_digest)):
        raise ImportError_("prompt_sha256 must be 64 lowercase hexadecimal characters")
    candidates = manifest.get("candidates")
    if not isinstance(candidates, list) or not 1 <= len(candidates) <= MAX_CANDIDATES:
        raise ImportError_("candidates must have 1–16 source PNGs")
    sources = []
    seen = set()
    for item in candidates:
        if not isinstance(item, dict) or set(item) != {"file"}:
            raise ImportError_("candidate source requires exactly a file path")
        relative = relative_path(item["file"])
        if not relative.name.lower().endswith(".png"):
            raise ImportError_("external candidates must be PNG files")
        key = relative.as_posix().casefold()
        if key in seen:
            raise ImportError_("duplicate case-insensitive candidate source")
        seen.add(key)
        sources.append(relative)
    return manifest, sources, hashlib.sha256(data).hexdigest()


def import_candidates(spec_path, output_dir, replace=False):
    if spec_path.is_symlink() or output_dir.is_symlink():
        raise ImportError_("spec and output must not be symlinks")
    spec_path = spec_path.resolve()
    output_dir = output_dir.resolve()
    root = spec_path.parent
    if (output_dir == root or output_dir.is_relative_to(root) or
            root.is_relative_to(output_dir)):
        raise ImportError_("candidate output must be separate from the source/spec tree")
    if output_dir.exists() and (not output_dir.is_dir() or not replace):
        raise ImportError_("output already exists; use --replace to rebuild")
    spec, sources, spec_digest = read_spec(spec_path)
    validated = []
    for index, relative in enumerate(sources, 1):
        candidate = root / relative
        resolved = candidate.resolve()
        if candidate.is_symlink() or not resolved.is_relative_to(root) or not resolved.is_file():
            raise ImportError_(f"candidate file missing or unsafe: {relative.as_posix()}")
        if resolved.stat().st_size > MAX_IMAGE_BYTES:
            raise ImportError_(f"candidate file exceeds size limit: {relative.as_posix()}")
        data = resolved.read_bytes()
        if len(data) > MAX_IMAGE_BYTES:
            raise ImportError_(f"candidate file exceeds size limit: {relative.as_posix()}")
        try:
            details = inspect_png(data)
        except AssetPrepError as exc:
            raise ImportError_(f"invalid PNG candidate: {relative.as_posix()}: {exc}") from exc
        if details["width"] > MAX_DIMENSION or details["height"] > MAX_DIMENSION:
            raise ImportError_("candidate image dimensions exceed limit")
        name = f"candidate-{index:03d}.png"
        validated.append((name, data, {
            "file": name,
            "sha256": hashlib.sha256(data).hexdigest(),
            "bytes": len(data),
            "width": details["width"],
            "height": details["height"],
            "seed": None,
            "cost_usd": None,
            "cost_estimated": False,
            "approved": False,
        }))
    receipt = {
        "version": 1,
        "state": "unreviewed",
        "approved": False,
        "eligible_for_packaging": False,
        "job_id": spec["job_id"],
        "provider": spec["provider"],
        "model": spec["model"],
        "prompt_sha256": spec["prompt_sha256"],
        "spec_sha256": spec_digest,
        "candidate_count": len(validated),
        "cost_usd": None,
        "cost_estimated": False,
        "candidates": [entry[2] for entry in validated],
    }

    output_dir.parent.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix=f".{output_dir.name}-import-", dir=output_dir.parent))
    backup = None
    try:
        for name, data, _ in validated:
            (temporary / name).write_bytes(data)
        (temporary / QUARANTINE_MARKER).write_text(
            json.dumps(receipt, sort_keys=True, indent=2) + "\n", encoding="utf-8"
        )
        if output_dir.exists():
            backup = Path(tempfile.mkdtemp(prefix=f".{output_dir.name}-backup-", dir=output_dir.parent))
            backup.rmdir()
            os.replace(output_dir, backup)
        try:
            os.replace(temporary, output_dir)
            temporary = None
        except OSError:
            if backup is not None:
                os.replace(backup, output_dir)
                backup = None
            raise
        if backup is not None:
            shutil.rmtree(backup, ignore_errors=True)
    finally:
        if temporary is not None and temporary.exists():
            shutil.rmtree(temporary, ignore_errors=True)
    return len(validated)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("spec", type=Path, help="JSON import manifest beside source PNGs")
    parser.add_argument("output_dir", type=Path, help="isolated unreviewed candidate directory")
    parser.add_argument("--replace", action="store_true")
    args = parser.parse_args(argv)
    try:
        count = import_candidates(args.spec, args.output_dir, replace=args.replace)
        print(f"Imported {count} UNREVIEWED external PNG candidate(s) into {args.output_dir}")
        return 0
    except (ImportError_, OSError) as exc:
        print(f"candidate_import: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
