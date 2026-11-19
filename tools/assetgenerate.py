#!/usr/bin/env python3
"""Generate quarantined image candidates via an explicit, provider-neutral port.

The offline mock is the default. An optional real image API provider requires
an explicit paid-generation flag and credentials. Nothing this command writes
is approved or directly suitable for packaging.
"""

import argparse
from dataclasses import dataclass
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import struct
import sys
import tempfile
from typing import Optional, Protocol
import zlib

from assetprep import AssetPrepError, inspect_png

QUARANTINE_MARKER = ".asset-pipeline-quarantine.json"
MAX_CANDIDATES = 16
MAX_IMAGE_SIDE = 1024
MAX_OUTPUT_BYTES = 32 * 1024 * 1024
MAX_SPEC_BYTES = 64 * 1024


class GenerationError(ValueError):
    """The generation request, provider result, or output target is invalid."""


@dataclass(frozen=True)
class GenerationRequest:
    job_id: str
    provider: str
    model: str
    prompt: str
    width: int
    height: int
    candidate_count: int
    seed: int
    max_cost_usd: float


@dataclass(frozen=True)
class GeneratedImage:
    png: bytes
    seed: Optional[int]
    cost_usd: float
    cost_estimated: bool = False


class GenerationProvider(Protocol):
    """Implementations are build-time-only and must not approve outputs."""

    name: str
    model: str

    def estimate_cost_usd(self, request: GenerationRequest) -> float:
        """Preflight allowance (not a guaranteed maximum invoice for paid APIs)."""

    def generate_candidates(self, request: GenerationRequest) -> list[GeneratedImage]:
        """Return candidates and measured costs; never publish to approved assets."""


def positive_int(value, name, low, high):
    if type(value) is not int or not (low <= value <= high):
        raise GenerationError(f"{name} must be an integer between {low} and {high}")
    return value


def load_request(spec_path: Path):
    if not spec_path.is_file() or spec_path.stat().st_size > MAX_SPEC_BYTES:
        raise GenerationError("generation spec is missing or too large")
    raw = spec_path.read_bytes()
    if len(raw) > MAX_SPEC_BYTES:
        raise GenerationError("generation spec exceeds size limit")
    try:
        obj = json.loads(raw)
    except (ValueError, UnicodeDecodeError) as exc:
        raise GenerationError(f"invalid generation JSON: {exc}") from exc
    if not isinstance(obj, dict) or type(obj.get("version")) is not int or obj["version"] != 1:
        raise GenerationError("generation spec version must be 1")
    allowed = {"version", "job_id", "provider", "model", "prompt", "width", "height",
               "candidate_count", "seed", "budget"}
    if set(obj) - allowed:
        raise GenerationError("unknown generation spec fields")
    job_id = obj.get("job_id")
    if not isinstance(job_id, str) or not re.fullmatch(r"[a-z0-9][a-z0-9_-]{0,63}", job_id):
        raise GenerationError("job_id must be a portable lower-case slug")
    prompt = obj.get("prompt")
    if not isinstance(prompt, str) or not prompt.strip() or len(prompt) > 8192:
        raise GenerationError("prompt must contain 1–8192 characters")
    budget = obj.get("budget")
    if not isinstance(budget, dict) or set(budget) != {"max_candidates", "max_cost_usd"}:
        raise GenerationError("budget requires max_candidates and max_cost_usd")
    cap = positive_int(budget["max_candidates"], "budget.max_candidates", 1, MAX_CANDIDATES)
    count = positive_int(obj.get("candidate_count"), "candidate_count", 1, cap)
    cost = budget["max_cost_usd"]
    if type(cost) not in (int, float) or not math.isfinite(cost) or not 0 <= cost <= 10000:
        raise GenerationError("budget.max_cost_usd must be finite and nonnegative")
    provider = obj.get("provider")
    model = obj.get("model")
    if not isinstance(provider, str) or not provider or not isinstance(model, str) or not model:
        raise GenerationError("provider and model are required strings")
    return (GenerationRequest(
        job_id, provider, model, prompt,
        positive_int(obj.get("width"), "width", 1, MAX_IMAGE_SIDE),
        positive_int(obj.get("height"), "height", 1, MAX_IMAGE_SIDE),
        count, positive_int(obj.get("seed"), "seed", 0, (1 << 32) - 1), float(cost),
    ), hashlib.sha256(raw).hexdigest())


def png_chunk(name: bytes, data: bytes):
    return struct.pack(">I", len(data)) + name + data + struct.pack(">I", zlib.crc32(name + data))


class MockCheckerProvider:
    """A deliberately recognizable synthetic fixture, NOT AI-generated art."""

    name = "mock"
    model = "checker-v1"

    def estimate_cost_usd(self, request: GenerationRequest) -> float:
        return 0.0

    def generate_candidates(self, request: GenerationRequest) -> list[GeneratedImage]:
        if request.width > 512 or request.height > 512:
            raise GenerationError("mock fixture dimensions must not exceed 512x512")
        output = []
        for index in range(request.candidate_count):
            seed = (request.seed + index) % (1 << 32)
            digest = hashlib.sha256(
                f"{request.prompt}|{seed}|{index}".encode("utf-8")
            ).digest()
            a = (digest[0], digest[1], digest[2], 255)
            b = (digest[3], digest[4], digest[5], 255)
            raw = bytearray()
            for y in range(request.height):
                raw.append(0)  # PNG filter type: none
                for x in range(request.width):
                    raw.extend(a if ((x // 8 + y // 8) & 1) == 0 else b)
            ihdr = struct.pack(">IIBBBBB", request.width, request.height, 8, 6, 0, 0, 0)
            png = (b"\x89PNG\r\n\x1a\n" + png_chunk(b"IHDR", ihdr) +
                   png_chunk(b"IDAT", zlib.compress(raw, level=9)) +
                   png_chunk(b"IEND", b""))
            output.append(GeneratedImage(png, seed, 0.0))
        return output



class OpenAIImagesProvider:
    """Opt-in paid Image API adapter; NOT enabled by generation JSON alone."""

    name = "openai-images"

    def __init__(self, model: str):
        self.model = model

    def estimate_cost_usd(self, request: GenerationRequest) -> float:
        from openai_image_provider import PREFLIGHT_ALLOWANCE_USD, SUPPORTED_MODELS
        if self.model not in SUPPORTED_MODELS:
            raise GenerationError("unsupported paid image model")
        if request.candidate_count != 1:
            raise GenerationError("paid v1 supports one image per explicit invocation")
        if (request.width, request.height) != (1024, 1024):
            raise GenerationError("paid v1 supports 1024x1024 only")
        if request.seed != 0:
            raise GenerationError("live model does not support seed; set seed to 0")
        return PREFLIGHT_ALLOWANCE_USD

    def generate_candidates(self, request: GenerationRequest) -> list[GeneratedImage]:
        from openai_image_provider import ImageProviderError, request_one_png
        try:
            png, estimated_cost = request_one_png(
                self.model, request.prompt, request.width, request.height
            )
        except ImageProviderError as exc:
            raise GenerationError(str(exc)) from exc
        # Image API does not expose a reproducible seed. Record null, not a fake seed.
        return [GeneratedImage(png, None, estimated_cost, True)]


# Explicit registration: never import modules based on untrusted spec fields.
PROVIDERS: dict[str, GenerationProvider] = {"mock": MockCheckerProvider()}



def validate_results(request, provider, results):
    if len(results) != request.candidate_count:
        raise GenerationError("provider returned an unexpected candidate count")
    total_cost = 0.0
    validated = []
    for index, candidate in enumerate(results, 1):
        if not isinstance(candidate, GeneratedImage) or not isinstance(candidate.png, bytes):
            raise GenerationError("provider returned a malformed candidate")
        if len(candidate.png) > MAX_OUTPUT_BYTES:
            raise GenerationError("candidate exceeds PNG size limit")
        if candidate.seed is None:
            if provider.name != "openai-images":
                raise GenerationError("only providers without seed support may return null seed")
        elif type(candidate.seed) is not int or candidate.seed < 0 or candidate.seed >= 1 << 32:
            raise GenerationError("candidate has invalid seed")
        if type(candidate.cost_usd) not in (float, int) or not math.isfinite(candidate.cost_usd) or candidate.cost_usd < 0:
            raise GenerationError("candidate has invalid cost")
        total_cost += candidate.cost_usd
        if total_cost > request.max_cost_usd:
            raise GenerationError("provider exceeded generation budget; outputs quarantined")
        try:
            png = inspect_png(candidate.png)
        except AssetPrepError as exc:
            raise GenerationError(f"provider candidate {index} failed PNG validation: {exc}") from exc
        if (png["width"], png["height"]) != (request.width, request.height):
            raise GenerationError("provider returned unexpected image dimensions")
        name = f"candidate-{index:03d}.png"
        validated.append((name, candidate.png, {
            "file": name,
            "sha256": hashlib.sha256(candidate.png).hexdigest(),
            "bytes": len(candidate.png),
            "width": png["width"],
            "height": png["height"],
            "seed": candidate.seed,
            "cost_usd": candidate.cost_usd,
            "cost_estimated": candidate.cost_estimated,
            "approved": False,
        }))
    return validated, total_cost


def generate(spec_path: Path, out_dir: Path, replace=False, allow_paid=False):
    if out_dir.is_symlink():
        raise GenerationError("output may not be a symlink")
    spec_path = spec_path.resolve()
    out_dir = out_dir.resolve()
    if spec_path.is_relative_to(out_dir) or out_dir == spec_path.parent:
        raise GenerationError("output cannot replace the spec or its containing directory")
    if out_dir.exists() and (not out_dir.is_dir() or not replace):
        raise GenerationError("output already exists; use --replace to replace it")
    request, spec_digest = load_request(spec_path)
    if request.provider == "openai-images":
        if not allow_paid:
            raise GenerationError("paid generation blocked; pass --allow-paid-generation explicitly")
        provider = OpenAIImagesProvider(request.model)
    else:
        provider = PROVIDERS.get(request.provider)
    if provider is None or provider.name != request.provider or provider.model != request.model:
        raise GenerationError("provider/model is not registered")
    estimate = provider.estimate_cost_usd(request)
    if type(estimate) not in (float, int) or not math.isfinite(estimate) or estimate < 0 or estimate > request.max_cost_usd:
        raise GenerationError("provider estimate exceeds generation budget")
    validated, actual_cost = validate_results(request, provider, provider.generate_candidates(request))
    receipt = {
        "version": 1,
        "state": "unreviewed",
        "approved": False,
        "eligible_for_packaging": False,
        "job_id": request.job_id,
        "provider": provider.name,
        "model": provider.model,
        "prompt_sha256": hashlib.sha256(request.prompt.encode("utf-8")).hexdigest(),
        "spec_sha256": spec_digest,
        "candidate_count": len(validated),
        "cost_usd": actual_cost,
        "cost_estimated": request.provider == "openai-images",
        "candidates": [item[2] for item in validated],
    }

    out_dir.parent.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix=f".{out_dir.name}-candidates-", dir=out_dir.parent))
    backup = None
    try:
        for name, data, _ in validated:
            (temporary / name).write_bytes(data)
        (temporary / QUARANTINE_MARKER).write_text(
            json.dumps(receipt, sort_keys=True, indent=2) + "\n", encoding="utf-8"
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
    return len(validated)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("spec", type=Path, help="JSON request; may contain a private prompt")
    parser.add_argument("out_dir", type=Path, help="separate unreviewed candidates directory")
    parser.add_argument("--replace", action="store_true")
    parser.add_argument("--allow-paid-generation", action="store_true",
                        help="explicitly enable one billable API request; requires OPENAI_API_KEY")
    args = parser.parse_args(argv)
    try:
        count = generate(args.spec, args.out_dir, args.replace,
                         allow_paid=args.allow_paid_generation)
        print(f"Created {count} UNREVIEWED candidate(s) in {args.out_dir}")
        return 0
    except (GenerationError, OSError) as exc:
        print(f"assetgenerate: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
