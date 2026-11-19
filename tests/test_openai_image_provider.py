"""Offline contract tests for the optional real OpenAI Images provider.

These tests never contact api.openai.com or consume billable image generations.
"""

import base64
from io import BytesIO
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import zlib

TOOLS = Path(__file__).resolve().parents[1] / "tools"
sys.path.insert(0, str(TOOLS))
import assetgenerate  # noqa: E402
import openai_image_provider as api  # noqa: E402


def valid_png(width=1024, height=1024):
    raw = (b"\x00" + b"\xff\x00\x00\xff" * width) * height
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + assetgenerate.png_chunk(b"IHDR", ihdr) +
            assetgenerate.png_chunk(b"IDAT", zlib.compress(raw, level=6)) +
            assetgenerate.png_chunk(b"IEND", b""))


class OpenAIImageProviderTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.spec = self.root / "request.json"
        self.candidates = self.root / "unreviewed"
        self.body = {
            "version": 1, "job_id": "torch-reference",
            "provider": "openai-images", "model": "gpt-image-2.6-flare",
            "prompt": "PRIVATE: original hand-painted fantasy wall torch isolated on transparency",
            "width": 1024, "height": 1024, "candidate_count": 1, "seed": 0,
            "budget": {"max_candidates": 1, "max_cost_usd": 0.50},
        }

    def write_spec(self):
        self.spec.write_text(json.dumps(self.body), encoding="utf-8")
        return self.spec

    def test_live_backend_cannot_run_without_explicit_flag(self):
        proc = subprocess.run([sys.executable, str(TOOLS / "assetgenerate.py"),
                               str(self.write_spec()), str(self.candidates)],
                              capture_output=True, text=True, timeout=10)
        self.assertNotEqual(proc.returncode, 0)
        self.assertIn("--allow-paid-generation", proc.stderr)
        self.assertNotIn(self.body["prompt"], proc.stderr)
        self.assertFalse(self.candidates.exists())

    def test_live_backend_requires_key_even_when_explicitly_enabled(self):
        env = dict(os.environ)
        env.pop("OPENAI_API_KEY", None)
        proc = subprocess.run([sys.executable, str(TOOLS / "assetgenerate.py"),
                               str(self.write_spec()), str(self.candidates),
                               "--allow-paid-generation"], env=env,
                              capture_output=True, text=True, timeout=10)
        self.assertNotEqual(proc.returncode, 0)
        self.assertIn("OPENAI_API_KEY", proc.stderr)
        self.assertFalse(self.candidates.exists())

    def test_live_backend_rejects_multiple_images_and_unsupported_dimensions(self):
        self.body["candidate_count"] = 2
        self.body["budget"]["max_candidates"] = 2
        with self.assertRaises(assetgenerate.GenerationError):
            assetgenerate.generate(self.write_spec(), self.candidates, allow_paid=True)
        self.body["candidate_count"] = 1
        self.body["width"] = 512
        self.body["height"] = 512
        with self.assertRaises(assetgenerate.GenerationError):
            assetgenerate.generate(self.write_spec(), self.candidates, allow_paid=True)

    def test_no_fake_seed_is_recorded_in_paid_receipt(self):
        png = valid_png()
        with patch.object(api, "request_one_png", return_value=(png, 0.03)) as fake:
            count = assetgenerate.generate(self.write_spec(), self.candidates, allow_paid=True)
        self.assertEqual(count, 1)
        fake.assert_called_once_with("gpt-image-2.6-flare", self.body["prompt"], 1024, 1024)
        marker = json.loads((self.candidates / assetgenerate.QUARANTINE_MARKER).read_text())
        self.assertEqual(marker["provider"], "openai-images")
        self.assertEqual(marker["candidates"][0]["seed"], None)
        self.assertTrue(marker["candidates"][0]["cost_estimated"])
        self.assertTrue(marker["cost_estimated"])
        self.assertFalse(marker["eligible_for_packaging"])
        self.assertNotIn(self.body["prompt"], json.dumps(marker))
        self.assertEqual((self.candidates / "candidate-001.png").read_bytes(), png)

    def test_preflight_allowance_is_not_zero_cost(self):
        self.body["budget"]["max_cost_usd"] = 0.20
        with self.assertRaises(assetgenerate.GenerationError):
            assetgenerate.generate(self.write_spec(), self.candidates, allow_paid=True)
        self.assertFalse(self.candidates.exists())

    def test_image_api_request_shape_and_usage_estimation(self):
        png = valid_png(8, 8)
        sent = []
        def fake_transport(key, payload):
            sent.append((key, json.loads(payload)))
            return json.dumps({
                "data": [{"b64_json": base64.b64encode(png).decode("ascii")}],
                "usage": {"input_tokens": 100, "output_tokens": 900},
            }).encode("utf-8")
        with patch.dict(os.environ, {"OPENAI_API_KEY": "private-test-key"}):
            result, estimated_cost = api.request_one_png(
                "gpt-image-2.6-flare", "isolated torch", 1024, 1024,
                transport=fake_transport)
        self.assertEqual(result, png)
        self.assertAlmostEqual(estimated_cost, 0.0275)
        self.assertEqual(sent[0][0], "private-test-key")
        params = sent[0][1]
        self.assertEqual(params["model"], "gpt-image-2.6-flare")
        self.assertEqual(params["size"], "1024x1024")
        self.assertEqual(params["background"], "transparent")
        self.assertEqual(params["output_format"], "png")
        self.assertEqual(params["quality"], "low")
        self.assertEqual(params["n"], 1)

    def test_missing_usage_is_not_misrepresented_as_free(self):
        png = valid_png(8, 8)
        def fake_transport(_key, _payload):
            return json.dumps({"data": [{"b64_json": base64.b64encode(png).decode("ascii")}]}).encode()
        with patch.dict(os.environ, {"OPENAI_API_KEY": "private-test-key"}):
            _, estimate = api.request_one_png(
                "gpt-image-2.6-flare", "isolated torch", 1024, 1024,
                transport=fake_transport)
        self.assertEqual(estimate, api.PREFLIGHT_ALLOWANCE_USD)

    def test_invalid_api_response_and_refusal_are_safe(self):
        with patch.dict(os.environ, {"OPENAI_API_KEY": "private-test-key"}):
            with self.assertRaises(api.ImageProviderError):
                api.request_one_png("gpt-image-2.6-flare", "test", 1024, 1024,
                                    transport=lambda *_: b'{"data":[]}')
            with self.assertRaises(api.ImageProviderError):
                api.request_one_png("gpt-image-2.6-flare", "test", 1024, 1024,
                                    transport=lambda *_: b'{"data":[{"b64_json":"?"}]}')
        with self.assertRaises(api.ImageProviderError):
            api.request_one_png("unlisted-model", "test", 1024, 1024)

    def test_http_errors_do_not_echo_secret_prompt_or_api_key(self):
        class BadResponse:
            status = 403
            def read(self, *args):
                return b'{"error":"private prompt or api key echoed"}'
        class FakeConnection:
            def __init__(self, *args, **kwargs):
                pass
            def request(self, *args, **kwargs):
                pass
            def getresponse(self):
                return BadResponse()
            def close(self):
                pass
        with patch.object(api.http.client, "HTTPSConnection", FakeConnection):
            with self.assertRaises(api.ImageProviderError) as error:
                api._send_request("PRIVATEKEY", b"PRIVATEPROMPT")
        self.assertIn("403", str(error.exception))
        self.assertNotIn("PRIVATE", str(error.exception))


if __name__ == "__main__":
    unittest.main()
