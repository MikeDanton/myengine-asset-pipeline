"""Review-safe import of externally generated PNGs into quarantined candidates."""

import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib

TOOL = Path(__file__).resolve().parents[1] / "tools/candidate_import.py"
MARKER = ".asset-pipeline-quarantine.json"


def make_png(width=12, height=9):
    def chunk(kind, data):
        return (struct.pack(">I", len(data)) + kind + data +
                struct.pack(">I", zlib.crc32(kind + data)))
    row = b"\x00" + b"\x63\x71\x80" * width
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) +
            chunk(b"IDAT", zlib.compress(row * height)) + chunk(b"IEND", b""))


class ExternalImageImportTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.src = self.root / "source"
        self.src.mkdir()
        (self.src / "art").mkdir()
        (self.src / "art/gate-stone.png").write_bytes(make_png())
        (self.src / "art/rune-stone.png").write_bytes(make_png(8, 8))
        self.spec_path = self.src / "import.json"
        self.out = self.root / "unreviewed/gate-stone"
        self.manifest = {
            "version": 1,
            "job_id": "forest-pass-gate-stone",
            "provider": "ChatGPT Images",
            "model": "model not exposed",
            "prompt_sha256": hashlib.sha256(b"Stone texture").hexdigest(),
            "candidates": [{"file": "art/gate-stone.png"}],
        }

    def run_cli(self, ok=True, replace=False):
        self.spec_path.write_text(json.dumps(self.manifest), encoding="utf-8")
        command = [sys.executable, str(TOOL), str(self.spec_path), str(self.out)]
        if replace:
            command.append("--replace")
        process = subprocess.run(command, capture_output=True, text=True, timeout=10)
        self.assertEqual(process.returncode == 0, ok, process.stdout + process.stderr)
        return process

    def test_import_receipt_and_source_integrity(self):
        src = (self.src / "art/gate-stone.png").read_bytes()
        self.run_cli()
        self.assertEqual((self.out / "candidate-001.png").read_bytes(), src)
        receipt = json.loads((self.out / MARKER).read_text())
        self.assertFalse(receipt["approved"])
        self.assertEqual(receipt["state"], "unreviewed")
        self.assertFalse(receipt["eligible_for_packaging"])
        self.assertEqual(receipt["provider"], "ChatGPT Images")
        self.assertEqual(receipt["model"], "model not exposed")
        self.assertEqual(receipt["prompt_sha256"], self.manifest["prompt_sha256"])
        self.assertEqual(receipt["candidate_count"], 1)
        self.assertIsNone(receipt["cost_usd"])
        self.assertIsNone(receipt["candidates"][0]["seed"])
        self.assertEqual(receipt["candidates"][0]["sha256"], hashlib.sha256(src).hexdigest())
        self.assertEqual((self.src / "art/gate-stone.png").read_bytes(), src)
        self.assertFalse((self.out / "assets.json").exists())

    def test_deterministic_rebuild_and_opt_in_replacement(self):
        self.run_cli()
        previous = (self.out / MARKER).read_bytes()
        self.run_cli(ok=False)
        self.run_cli(replace=True)
        self.assertEqual(previous, (self.out / MARKER).read_bytes())

    def test_multiple_images_preserve_order_and_stable_filenames(self):
        self.manifest["candidates"].append({"file": "art/rune-stone.png"})
        self.run_cli()
        receipt = json.loads((self.out / MARKER).read_text())
        self.assertEqual(receipt["candidate_count"], 2)
        self.assertEqual([e["file"] for e in receipt["candidates"]],
                         ["candidate-001.png", "candidate-002.png"])
        self.assertEqual(receipt["candidates"][1]["width"], 8)

    def test_invalid_manifest_preserves_previously_imported_candidates(self):
        self.run_cli()
        before = (self.out / "candidate-001.png").read_bytes()
        self.manifest["prompt_sha256"] = "not-a-hash"
        self.run_cli(ok=False, replace=True)
        self.assertEqual(before, (self.out / "candidate-001.png").read_bytes())

    def test_rejects_parent_paths_and_case_collisions(self):
        for path in ("../secret.png", "/etc/hosts", "art\\gate-stone.png",
                     "art/../secret.png", "art/not-a-png.jpeg"):
            with self.subTest(path=path):
                self.manifest["candidates"] = [{"file": path}]
                self.run_cli(ok=False)
        self.manifest["candidates"] = [
            {"file": "art/gate-stone.png"}, {"file": "ART/GATE-STONE.PNG"}
        ]
        self.run_cli(ok=False)

    def test_rejects_missing_and_corrupted_png(self):
        (self.src / "art/gate-stone.png").write_bytes(b"\x89PNG\r\n\x1a\nBROKEN")
        self.run_cli(ok=False)
        self.assertFalse(self.out.exists())
        self.manifest["candidates"] = [{"file": "no-such-file.png"}]
        self.run_cli(ok=False)

    def test_rejects_clobbering_source_tree(self):
        self.out = self.src
        self.run_cli(ok=False, replace=True)
        self.assertTrue((self.src / "art/gate-stone.png").exists())
        self.out = self.src / "build"
        self.run_cli(ok=False)
        self.assertFalse(self.out.exists())
        self.out = self.root
        self.run_cli(ok=False, replace=True)
        self.assertTrue(self.spec_path.exists())

    def test_symlink_source_escape_rejected(self):
        outside = self.root / "outside.png"
        outside.write_bytes(make_png())
        source = self.src / "art/gate-stone.png"
        source.unlink()
        try:
            source.symlink_to(outside)
        except (NotImplementedError, OSError):
            self.skipTest("symlinks unavailable")
        self.run_cli(ok=False)
        self.assertFalse(self.out.exists())

    def test_imported_candidates_are_reviewable_when_pillow_installed(self):
        try:
            from PIL import Image
        except ImportError:
            self.skipTest("Pillow required for visual contact sheet")
        self.run_cli()
        destination = self.root / "review"
        cli = Path(__file__).resolve().parents[1] / "tools/candidate_review.py"
        result = subprocess.run(
            [sys.executable, str(cli), str(self.out), str(destination)],
            capture_output=True, text=True, timeout=12)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        with Image.open(destination / "contact-sheet.png") as sheet:
            self.assertGreater(sheet.width, 0)
        review_receipt = json.loads((destination / MARKER).read_text())
        self.assertFalse(review_receipt["approved"])
        self.assertEqual(review_receipt["candidates"][0]["sha256"],
                         hashlib.sha256((self.out / "candidate-001.png").read_bytes()).hexdigest())

    def test_no_api_invocation_or_secret_prompts(self):
        self.run_cli()
        marker = (self.out / MARKER).read_text()
        self.assertNotIn("Stone texture", marker)
        self.assertNotIn("OPENAI_API_KEY", marker)


if __name__ == "__main__":
    unittest.main()
