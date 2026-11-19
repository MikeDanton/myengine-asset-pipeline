"""End-to-end offline checks for the review-only candidate contact sheet."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

try:
    from PIL import Image
except ImportError:
    Image = None

TOOLS = Path(__file__).resolve().parents[1] / "tools"
MARKER = ".asset-pipeline-quarantine.json"


@unittest.skipUnless(Image is not None, "Pillow required for contact-sheet tests")
class CandidateReviewTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.spec = self.root / "generation.json"
        self.candidates = self.root / "build/unreviewed/torch"
        self.review_dir = self.root / "build/reviews/torch"
        self.spec.write_text(json.dumps({
            "version": 1, "job_id": "torch-test",
            "provider": "mock", "model": "checker-v1",
            "prompt": "Secret prompt must never be included in review metadata",
            "width": 16, "height": 8, "candidate_count": 3, "seed": 42,
            "budget": {"max_candidates": 3, "max_cost_usd": 0}
        }), encoding="utf-8")
        result = subprocess.run([sys.executable, str(TOOLS / "assetgenerate.py"),
                                 str(self.spec), str(self.candidates)],
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)

    def run_review(self, ok=True, replace=True, *opts):
        command = [sys.executable, str(TOOLS / "candidate_review.py"),
                   str(self.candidates), str(self.review_dir), *opts]
        if replace:
            command.append("--replace")
        result = subprocess.run(command, capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode == 0, ok, result.stdout + result.stderr)
        return result

    def test_contact_sheet_and_provenance_are_review_only(self):
        self.run_review()
        marker = json.loads((self.review_dir / MARKER).read_text())
        self.assertEqual(marker["kind"], "candidate-contact-sheet")
        self.assertEqual(marker["state"], "unreviewed")
        self.assertFalse(marker["approved"])
        self.assertFalse(marker["eligible_for_packaging"])
        self.assertEqual(len(marker["candidates"]), 3)
        source = (self.candidates / MARKER).read_bytes()
        self.assertEqual(marker["source_receipt_sha256"], hashlib.sha256(source).hexdigest())
        self.assertNotIn("Secret prompt", (self.review_dir / MARKER).read_text())
        with Image.open(self.review_dir / "contact-sheet.png") as image:
            self.assertEqual(image.mode, "RGB")
            self.assertEqual(image.size, (636, 296))
        self.assertFalse((self.review_dir / "assets.json").exists())

    def test_deterministic_preview_and_metadata(self):
        self.run_review()
        a = (self.review_dir / "contact-sheet.png").read_bytes()
        b = (self.review_dir / MARKER).read_bytes()
        self.run_review()
        self.assertEqual(a, (self.review_dir / "contact-sheet.png").read_bytes())
        self.assertEqual(b, (self.review_dir / MARKER).read_bytes())

    def test_changed_candidate_is_rejected_and_preserves_prior_review(self):
        self.run_review()
        original = (self.review_dir / "contact-sheet.png").read_bytes()
        (self.candidates / "candidate-002.png").write_bytes(b"corrupt")
        self.run_review(ok=False)
        self.assertEqual(original, (self.review_dir / "contact-sheet.png").read_bytes())

    def test_absent_marker_and_untracked_png_fail_closed(self):
        (self.candidates / MARKER).unlink()
        self.run_review(ok=False)
        # Recreate a known candidate batch.
        self.candidates.parent.mkdir(parents=True, exist_ok=True)
        proc = subprocess.run([sys.executable, str(TOOLS / "assetgenerate.py"),
                               str(self.spec), str(self.candidates), "--replace"],
                              capture_output=True, text=True, timeout=10)
        self.assertEqual(proc.returncode, 0, proc.stderr)
        (self.candidates / "untracked.png").write_bytes(b"image")
        self.run_review(ok=False)

    def test_invalid_output_overlap_rejected(self):
        self.review_dir = self.candidates / "previews"
        self.run_review(ok=False)
        self.assertFalse(self.review_dir.exists())
        self.review_dir = self.root
        self.run_review(ok=False)
        self.assertTrue(self.spec.exists())

    def test_no_implicit_overwrite_and_invalid_layout(self):
        self.run_review()
        self.run_review(ok=False, replace=False)
        self.run_review(False, True, "--columns", "0")
        self.run_review(False, True, "--tile-size", "24")

    def test_source_symlink_rejected(self):
        image = self.candidates / "candidate-003.png"
        image.unlink()
        try:
            image.symlink_to(self.root / "nonexistent.png")
        except (OSError, NotImplementedError):
            self.skipTest("symlinks unavailable")
        self.run_review(ok=False)


if __name__ == "__main__":
    unittest.main()
