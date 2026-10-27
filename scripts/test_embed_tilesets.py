"""Embedded assets must match the current, supported tileset PNGs."""
import pathlib
import shutil
import subprocess
import sys
import tempfile
import unittest


class EmbeddedTilesetsTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = pathlib.Path(self.temporary.name)
        source = pathlib.Path(__file__).resolve().parent.parent
        (self.root / 'scripts').mkdir()
        shutil.copy(source / 'scripts/embed-tilesets.py', self.root / 'scripts')
        shutil.copytree(source / 'textures/tilesets', self.root / 'textures/tilesets')
        (self.root / 'src/graphics').mkdir(parents=True)
        shutil.copy(source / 'src/graphics/tileset_assets.h', self.root / 'src/graphics')
        self.png = next((self.root / 'textures/tilesets').glob('*.png'))

    def check_assets(self):
        return subprocess.run([sys.executable, 'scripts/embed-tilesets.py', '--check'],
                              cwd=self.root, capture_output=True, text=True)

    def test_current_assets_pass_without_retired_files(self):
        self.assertFalse((self.root / 'textures/tilesets/TILES_Zhelkas.16').exists())
        self.assertFalse((self.root / 'textures/tilesets/Ultima_5_Tiles_Zhelkas.png').exists())
        original = self.png.read_bytes()
        result = self.check_assets()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(self.png.read_bytes(), original)

    def test_changed_artwork_requires_regeneration(self):
        self.png.write_bytes(self.png.read_bytes() + b'changed')
        result = self.check_assets()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Tileset bytes are stale', result.stderr)
        subprocess.run([sys.executable, 'scripts/embed-tilesets.py'],
                       cwd=self.root, check=True)
        result = self.check_assets()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_stale_embedded_bytes_are_still_rejected(self):
        header = self.root / 'src/graphics/tileset_assets.h'
        header.write_text(header.read_text() + '// stale\n')
        result = self.check_assets()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Tileset bytes are stale', result.stderr)


if __name__ == '__main__':
    unittest.main()
