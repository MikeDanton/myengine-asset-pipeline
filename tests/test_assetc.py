import pathlib
import subprocess
import sys
import tempfile
import unittest

ASSETC = sys.argv.pop(1)
ZSTD = sys.argv.pop(1) == '1'

class AssetCompilerTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = pathlib.Path(self.tmp.name)
        self.assets = self.root / 'assets'
        self.assets.mkdir()
        self.output = self.root / 'output.pak'
        (self.assets / 'z.txt').write_text('z' * 4096)
        (self.assets / 'a.txt').write_text('hello')

    def run_assetc(self, *options, ok=True):
        result = subprocess.run([ASSETC, str(self.assets), str(self.output), *options], capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode == 0, ok, result.stdout + result.stderr)
        return result

    def test_policy_and_failure_preserve_output(self):
        self.run_assetc()
        original = self.output.read_bytes()
        self.assertIn('skipping', self.run_assetc().stdout)
        if ZSTD:
            self.assertNotIn('skipping', self.run_assetc('--compress').stdout)
            self.assertNotEqual(original, self.output.read_bytes())
            self.assertIn('skipping', self.run_assetc('--compress').stdout)
            self.run_assetc()
            self.assertEqual(original, self.output.read_bytes())
        else:
            self.run_assetc('--compress', ok=False)
            self.assertEqual(original, self.output.read_bytes())

    def test_deterministic_order(self):
        self.run_assetc()
        original = self.output.read_bytes()
        for file in self.assets.iterdir():
            file.unlink()
        (self.assets / 'a.txt').write_text('hello')
        (self.assets / 'z.txt').write_text('z' * 4096)
        self.output.unlink()
        self.run_assetc()
        self.assertEqual(original, self.output.read_bytes())

    def test_output_and_cache_inside_source_are_excluded(self):
        self.output = self.assets / 'output.pak'
        self.run_assetc()
        self.assertIn('skipping', self.run_assetc().stdout)

    def test_corrupted_output_invalidates_cache(self):
        self.run_assetc()
        original = self.output.read_bytes()
        self.output.write_bytes(b'broken')
        self.assertNotIn('skipping', self.run_assetc().stdout)
        self.assertEqual(original, self.output.read_bytes())

    def test_cache_output_alias_rejected(self):
        self.run_assetc()
        original = self.output.read_bytes()
        self.run_assetc(str(self.output), ok=False)
        self.assertEqual(original, self.output.read_bytes())

    def test_empty_tree_cache(self):
        for file in self.assets.iterdir():
            file.unlink()
        self.run_assetc()
        self.assertIn('skipping', self.run_assetc().stdout)

if __name__ == '__main__':
    unittest.main()
