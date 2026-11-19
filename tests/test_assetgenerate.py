"""Quarantine/cost contracts for provider-neutral candidate generation (stdlib only)."""

import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

TOOLS = Path(__file__).resolve().parents[1] / "tools"
sys.path.insert(0, str(TOOLS))
import assetgenerate  # noqa: E402


class CandidateGenerationTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.spec = self.root / "request.json"
        self.output = self.root / "build/candidates/torch"
        self.request = {
            "version": 1,
            "job_id": "platformer-torch",
            "provider": "mock",
            "model": "checker-v1",
            "prompt": "A PRIVATE staging prompt, never in the receipt",
            "width": 16,
            "height": 8,
            "candidate_count": 2,
            "seed": 42,
            "budget": {"max_candidates": 3, "max_cost_usd": 0.0},
        }

    def run_cli(self, ok=True, replace=True):
        self.spec.write_text(json.dumps(self.request), encoding="utf-8")
        command = [sys.executable, str(TOOLS / "assetgenerate.py"),
                   str(self.spec), str(self.output)]
        if replace:
            command.append("--replace")
        proc = subprocess.run(command, capture_output=True, text=True, timeout=10)
        self.assertEqual(proc.returncode == 0, ok, proc.stdout + proc.stderr)
        return proc

    def test_determinism_quarantine_and_no_prompt_disclosure(self):
        self.run_cli()
        marker = self.output / ".asset-pipeline-quarantine.json"
        before = marker.read_bytes()
        images = sorted(self.output.glob("*.png"))
        self.assertEqual([p.name for p in images], ["candidate-001.png", "candidate-002.png"])
        blobs = [p.read_bytes() for p in images]
        receipt = json.loads(before)
        self.assertEqual(receipt["state"], "unreviewed")
        self.assertFalse(receipt["approved"])
        self.assertFalse(receipt["eligible_for_packaging"])
        self.assertEqual(receipt["prompt_sha256"], hashlib.sha256(self.request["prompt"].encode()).hexdigest())
        self.assertEqual(receipt["cost_usd"], 0.0)
        self.assertNotIn(self.request["prompt"], before.decode("utf-8"))
        self.assertEqual([v["sha256"] for v in receipt["candidates"]],
                         [hashlib.sha256(blob).hexdigest() for blob in blobs])
        for blob in blobs:
            self.assertEqual(assetgenerate.inspect_png(blob)["width"], 16)
        self.run_cli()
        self.assertEqual(before, marker.read_bytes())
        self.assertEqual(blobs, [p.read_bytes() for p in sorted(self.output.glob("*.png"))])

    def test_spec_validation_does_not_destroy_existing_candidates(self):
        self.run_cli()
        previous = (self.output / "candidate-001.png").read_bytes()
        self.request["candidate_count"] = 4  # above declared budget
        self.run_cli(ok=False)
        self.assertEqual(previous, (self.output / "candidate-001.png").read_bytes())

    def test_invalid_models_and_budget(self):
        self.request["model"] = "hosted-not-installed"
        self.run_cli(ok=False)
        self.assertFalse(self.output.exists())
        self.request["model"] = "checker-v1"
        self.request["budget"]["max_cost_usd"] = -1
        self.run_cli(ok=False)
        self.request["budget"]["max_cost_usd"] = True
        self.run_cli(ok=False)
        self.request["budget"]["max_cost_usd"] = float("nan")
        self.run_cli(ok=False)
        self.request["budget"]["max_cost_usd"] = 0

    def test_rejects_overwriting_existing_output_without_flag(self):
        self.run_cli()
        self.run_cli(ok=False, replace=False)

    def test_rejects_unsafe_job_and_output(self):
        self.request["job_id"] = "../unsafe"
        self.run_cli(ok=False)
        self.request["job_id"] = "platformer-torch"
        self.output = self.root
        self.run_cli(ok=False)
        self.assertTrue(self.spec.is_file())

    def test_no_auto_promotion_into_approved_tree(self):
        self.run_cli()
        self.assertFalse((self.root / "approved").exists())
        self.assertFalse((self.root / "build/approved").exists())
        self.assertFalse((self.output / "assets.json").exists())

    def test_provider_outputs_are_validated_before_writing(self):
        request, _ = assetgenerate.load_request(self._write_spec())
        good = assetgenerate.MockCheckerProvider().generate_candidates(request)
        bad = [assetgenerate.GeneratedImage(good[0].png, good[0].seed, 1.0), good[1]]
        with self.assertRaises(assetgenerate.GenerationError):
            assetgenerate.validate_results(request, assetgenerate.MockCheckerProvider(), bad)
        bad = [assetgenerate.GeneratedImage(b"not png", 42, 0.0), good[1]]
        with self.assertRaises(assetgenerate.GenerationError):
            assetgenerate.validate_results(request, assetgenerate.MockCheckerProvider(), bad)
        with self.assertRaises(assetgenerate.GenerationError):
            assetgenerate.validate_results(request, assetgenerate.MockCheckerProvider(), good[:1])

    def _write_spec(self):
        self.spec.write_text(json.dumps(self.request), encoding="utf-8")
        return self.spec

    def test_rejects_symlink_output(self):
        link = self.root / "output-link"
        try:
            link.symlink_to(self.root / "elsewhere", target_is_directory=True)
        except (OSError, NotImplementedError):
            self.skipTest("symlinks unavailable")
        self.output = link
        self.run_cli(ok=False)


if __name__ == "__main__":
    unittest.main()
