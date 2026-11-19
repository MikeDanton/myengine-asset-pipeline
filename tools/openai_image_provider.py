"""Optional, opt-in build-time OpenAI Images API transport.

No SDK dependency. This module is imported only for explicit paid generation.
The endpoint, model set and request shape are intentionally fixed, and there
are no automatic retries (a retry could incur another paid image).
"""

import base64
import binascii
import http.client
import json
import math
import os

API_HOST = "api.openai.com"
API_PATH = "/v1/images/generations"
MAX_RESPONSE_BYTES = 48 * 1024 * 1024
MAX_IMAGE_BYTES = 32 * 1024 * 1024
SUPPORTED_MODELS = frozenset({
    "gpt-image-2.6-flare",
    "gpt-image-2.6-sunburst",
})
PREFLIGHT_ALLOWANCE_USD = 0.50  # Authorization threshold, NOT a guaranteed price cap.


class ImageProviderError(ValueError):
    pass


def request_one_png(model, prompt, width, height, *, transport=None):
    """Request ONE low-quality, transparent PNG; return bytes and estimated USD.

    The live service does not expose a deterministic image seed in this path.
    API usage estimates are not a billing guarantee. Missing token usage falls
    back to the preflight allowance rather than inventing a zero-dollar cost.
    """
    if model not in SUPPORTED_MODELS:
        raise ImageProviderError("unsupported live image model")
    if (width, height) != (1024, 1024):
        raise ImageProviderError("v1 live adapter supports 1024x1024 only")
    if not isinstance(prompt, str) or not prompt.strip():
        raise ImageProviderError("nonempty prompt required")
    token = os.environ.get("OPENAI_API_KEY")
    if not token or any(c.isspace() for c in token):
        raise ImageProviderError("OPENAI_API_KEY is required for paid generation")
    payload = json.dumps({
        "model": model,
        "prompt": prompt,
        "n": 1,
        "size": "1024x1024",
        "quality": "low",
        "background": "transparent",
        "output_format": "png",
    }).encode("utf-8")

    if transport is None:
        transport = _send_request
    raw = transport(token, payload)
    if len(raw) > MAX_RESPONSE_BYTES:
        raise ImageProviderError("image API response exceeded configured size")
    try:
        response = json.loads(raw)
    except (UnicodeDecodeError, ValueError) as exc:
        raise ImageProviderError("image API returned invalid JSON") from exc
    entries = response.get("data") if isinstance(response, dict) else None
    if not isinstance(entries, list) or len(entries) != 1 or not isinstance(entries[0], dict):
        raise ImageProviderError("image API response must contain one image")
    image64 = entries[0].get("b64_json")
    if not isinstance(image64, str) or len(image64) > MAX_RESPONSE_BYTES:
        raise ImageProviderError("image API response missing image bytes")
    try:
        png = base64.b64decode(image64, validate=True)
    except (ValueError, binascii.Error) as exc:
        raise ImageProviderError("image API returned malformed base64") from exc
    if len(png) > MAX_IMAGE_BYTES:
        raise ImageProviderError("image API returned an oversized image")
    usage = response.get("usage")
    if isinstance(usage, dict) and (type(usage.get("input_tokens")) is int and
                                    type(usage.get("output_tokens")) is int and
                                    usage["input_tokens"] >= 0 and usage["output_tokens"] >= 0):
        # Published standard GPT-Image-2.6 rates as of 2026-11-19:
        # $5 / million input tokens, $30 / million output tokens.
        # An estimate for developer review only; actual invoice may differ.
        cost_estimate = ((usage["input_tokens"] * 5.0) +
                         (usage["output_tokens"] * 30.0)) / 1_000_000
    else:
        cost_estimate = PREFLIGHT_ALLOWANCE_USD
    if not math.isfinite(cost_estimate):
        raise ImageProviderError("invalid usage estimate")
    return png, cost_estimate


def _send_request(token, payload):
    conn = http.client.HTTPSConnection(API_HOST, timeout=120)
    try:
        conn.request("POST", API_PATH, body=payload, headers={
            "Authorization": f"Bearer {token}",
            "Content-Type": "application/json",
            "Accept": "application/json",
        })
        response = conn.getresponse()
        # Never include response bodies or request payloads in error messages:
        # provider errors may echo a private prompt.
        if response.status != 200:
            raise ImageProviderError(f"image API returned HTTP {response.status} (no retry)")
        raw = response.read(MAX_RESPONSE_BYTES + 1)
        if len(raw) > MAX_RESPONSE_BYTES:
            raise ImageProviderError("image API response exceeded configured size")
        return raw
    except (TimeoutError, ConnectionError, OSError) as exc:
        raise ImageProviderError("image API transport failed (no retry)") from exc
    finally:
        conn.close()
