"""Contract tests for staging human and AI source files without network access."""
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib

TOOL = Path(__file__).resolve().parents[1] / "tools/assetprep.py"


def png(width=2, height=2, alpha=True):
    color = 6 if alpha else 2
    pixel = bytes([255, 64, 0, 255] if alpha else [255, 64, 0])
    raw = (b"\x00" + pixel * width) * height
    def chunk(name, data):
        return struct.pack(">I", len(data)) + name + data + struct.pack(">I", zlib.crc32(name + data))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, color, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


class AssetPrepTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / "source").mkdir()
        (self.root / "source/hand.png").write_bytes(png())
        (self.root / "source/ai.png").write_bytes(png())
        self.manifest = self.root / "assets.json"
        self.out = self.root / "build/staged"
        self.provenance = self.root / "build/provenance.json"
        self.human = {"key": "sprites/human.png", "kind": "png", "approved": True,
                      "image": {"width": 2, "height": 2, "alpha": True},
                      "source": {"adapter": "human-file", "file": "source/hand.png",
                                 "creator": "Example artist", "license": "CC0-1.0"}}
        self.ai = {"key": "sprites/ai.png", "kind": "png", "approved": True,
                   "source": {"adapter": "ai-import", "file": "source/ai.png",
                              "provider": "example", "model": "example-v1",
                              "prompt_sha256": hashlib.sha256(b"torch prompt").hexdigest(),
                              "rights_note": "Example reviewed before distribution", "rights_reviewed": True,
                              "reviewed_by": "maintainer"}}

    def run_pipeline(self, assets, ok=True):
        self.manifest.write_text(json.dumps({"version": 1, "assets": assets}))
        result = subprocess.run([sys.executable, str(TOOL), str(self.manifest), str(self.out),
                                 "--provenance", str(self.provenance), "--replace"], capture_output=True, text=True)
        self.assertEqual(result.returncode == 0, ok, result.stderr or result.stdout)
        return result

    def test_both_adapters_and_deterministic_receipt(self):
        self.run_pipeline([self.human, self.ai])
        contents = (self.out / "sprites/human.png").read_bytes()
        self.assertEqual(contents, (self.root / "source/hand.png").read_bytes())
        self.assertFalse((self.out / "provenance.json").exists())
        receipt = self.provenance.read_bytes()
        entries = json.loads(receipt)["assets"]
        self.assertEqual([e["key"] for e in entries], ["sprites/ai.png", "sprites/human.png"])
        self.assertEqual(entries[0]["source"]["adapter"], "ai-import")
        self.assertEqual(entries[1]["sha256"], hashlib.sha256(contents).hexdigest())
        self.run_pipeline([self.ai, self.human])
        self.assertEqual(receipt, self.provenance.read_bytes())

    def test_unreviewed_ai_is_rejected_without_output(self):
        self.ai["source"]["rights_reviewed"] = False
        self.run_pipeline([self.ai], ok=False)
        self.assertFalse(self.out.exists())

    def test_approval_gate_preserves_previous_stage(self):
        self.run_pipeline([self.human])
        before = (self.out / "sprites/human.png").read_bytes()
        self.human["approved"] = False
        self.run_pipeline([self.human], ok=False)
        self.assertEqual(before, (self.out / "sprites/human.png").read_bytes())

    def test_traversal_case_collision_and_report_inside_pack_rejected(self):
        self.human["key"] = "../escape.png"
        self.run_pipeline([self.human], ok=False)
        self.human["key"] = "sprites/a.png"
        self.ai["key"] = "SPRITES/A.PNG"
        self.run_pipeline([self.ai, self.human], ok=False)
        self.assertFalse(self.out.exists())

    def test_png_crc_dimensions_and_alpha(self):
        self.human["image"]["width"] = 10
        self.run_pipeline([self.human], ok=False)
        self.human["image"]["width"] = 2
        raw = (self.root / "source/hand.png").read_bytes()
        (self.root / "source/hand.png").write_bytes(raw[:-4] + b"BAD!")
        self.run_pipeline([self.human], ok=False)
        (self.root / "source/hand.png").write_bytes(png(alpha=False))
        self.run_pipeline([self.human], ok=False)

    def test_source_escape_rejected(self):
        (self.root / "source/hand.png").unlink()
        try:
            (self.root / "source/hand.png").symlink_to("/etc/hosts")
        except (OSError, NotImplementedError):
            self.skipTest("symlink creation unavailable")
        self.run_pipeline([self.human], ok=False)

    def test_provenance_must_not_be_packed(self):
        self.provenance = self.out / "metadata.json"
        self.run_pipeline([self.human], ok=False)
        self.assertFalse(self.out.exists())


if __name__ == "__main__":
    unittest.main()
