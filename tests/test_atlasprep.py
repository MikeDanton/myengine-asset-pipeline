"""Offline regression tests for the optional Pillow atlas authoring tool."""
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

TOOL = Path(__file__).resolve().parents[1] / "tools/atlasprep.py"


@unittest.skipUnless(Image is not None, "Pillow is required for atlasprep tests")
class AtlasPrepTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.approved = self.root / "approved"
        (self.approved / "sprites").mkdir(parents=True)
        self.layout_file = self.root / "atlas.json"
        self.out = self.root / "pack-ready"
        # A horizontal strip of two frames with distinguishable edges.
        strip = Image.new("RGBA", (4, 2), (0, 0, 0, 0))
        for x in range(2):
            for y in range(2):
                strip.putpixel((x, y), (255, 0, 0, 255))
                strip.putpixel((x + 2, y), (0, 0, 255, 128))
        strip.save(self.approved / "sprites/torch.png")
        self.layout = {"version": 1, "atlas_key": "atlases/world.png",
                       "metadata_key": "atlases/world.json",
                       "atlas_width": 8, "max_height": 8, "padding": 1,
                       "sprites": [{"id": "torch_idle", "source_key": "sprites/torch.png",
                                    "frame_width": 2, "frame_height": 2,
                                    "frame_count": 2, "pivot": [1, 2]}]}

    def run_tool(self, ok=True, replace=True):
        self.layout_file.write_text(json.dumps(self.layout))
        cmd = [sys.executable, str(TOOL), str(self.approved),
               str(self.layout_file), str(self.out)]
        if replace:
            cmd.append("--replace")
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=12)
        self.assertEqual(result.returncode == 0, ok, result.stderr or result.stdout)
        return result

    def test_two_frames_atlas_metadata_and_extrusion(self):
        self.run_tool()
        meta = json.loads((self.out / "atlases/world.json").read_text())
        self.assertEqual((meta["width"], meta["height"]), (8, 8))
        frames = meta["frames"]
        self.assertEqual([(x["frame"], x["x"], x["y"]) for x in frames],
                         [(0, 1, 1), (1, 5, 1)])
        self.assertEqual(frames[0]["pivot"], [1, 2])
        with Image.open(self.out / "atlases/world.png") as image:
            self.assertEqual(image.mode, "RGBA")
            self.assertEqual(image.getpixel((1, 1)), (255, 0, 0, 255))
            self.assertEqual(image.getpixel((5, 1)), (0, 0, 255, 128))
            # Edge extrusion fills padding, including corner texels.
            self.assertEqual(image.getpixel((0, 0)), (255, 0, 0, 255))
            self.assertEqual(image.getpixel((7, 0)), (0, 0, 255, 128))
        self.assertEqual((self.approved / "sprites/torch.png").read_bytes(),
                         (self.out / "sprites/torch.png").read_bytes())

    def test_deterministic_bytes(self):
        self.run_tool()
        first_png = (self.out / "atlases/world.png").read_bytes()
        first_json = (self.out / "atlases/world.json").read_bytes()
        self.layout["sprites"].append({"id": "other", "source_key": "sprites/torch.png",
                                        "frame_width": 2, "frame_height": 2,
                                        "frame_count": 2, "pivot": [0, 0]})
        self.run_tool()
        second_png = (self.out / "atlases/world.png").read_bytes()
        self.run_tool()
        self.assertEqual(second_png, (self.out / "atlases/world.png").read_bytes())
        # Restore the original layout and verify source-input determinism.
        self.layout["sprites"].pop()
        self.run_tool()
        self.assertEqual(first_png, (self.out / "atlases/world.png").read_bytes())
        self.assertEqual(first_json, (self.out / "atlases/world.json").read_bytes())

    def test_refuses_unapproved_source_directory_mutation(self):
        self.run_tool()
        self.run_tool(ok=False, replace=False)
        self.assertFalse((self.approved / "atlases/world.png").exists())

    def test_invalid_frame_size_preserves_previous_output(self):
        self.run_tool()
        original = (self.out / "atlases/world.png").read_bytes()
        self.layout["sprites"][0]["frame_width"] = 3
        self.run_tool(ok=False)
        self.assertEqual(original, (self.out / "atlases/world.png").read_bytes())

    def test_duplicate_and_unsafe_keys(self):
        self.layout["sprites"].append(dict(self.layout["sprites"][0], id="TORCH_IDLE"))
        self.run_tool(ok=False)
        self.layout["sprites"].pop()
        self.layout["sprites"][0]["source_key"] = "../outside.png"
        self.run_tool(ok=False)
        self.layout["sprites"][0]["source_key"] = "sprites/torch.png"
        self.layout["atlas_key"] = "sprites/torch.png"
        self.run_tool(ok=False)

    def test_overflow_is_rejected(self):
        self.layout["max_height"] = 3
        self.run_tool(ok=False)
        self.assertFalse(self.out.exists())

    def test_invalid_or_truncated_png_rejected(self):
        (self.approved / "sprites/torch.png").write_bytes(b"\x89PNG\r\n\x1a\nbroken")
        self.run_tool(ok=False)

    def test_vertical_strip(self):
        with Image.open(self.approved / "sprites/torch.png") as image:
            top = image.crop((0, 0, 2, 2))
            bottom = image.crop((2, 0, 4, 2))
        vertical = Image.new("RGBA", (2, 4))
        vertical.paste(top, (0, 0))
        vertical.paste(bottom, (0, 2))
        vertical.save(self.approved / "sprites/torch.png")
        self.layout["sprites"][0]["direction"] = "vertical"
        self.run_tool()
        self.assertEqual(len(json.loads((self.out / "atlases/world.json").read_text())["frames"]), 2)

    def test_symlink_in_approved_tree_is_rejected(self):
        candidate = self.approved / "sprites/link.png"
        try:
            candidate.symlink_to(self.root / "outside.png")
        except (OSError, NotImplementedError):
            self.skipTest("symlinks unsupported")
        self.run_tool(ok=False)


if __name__ == "__main__":
    unittest.main()
