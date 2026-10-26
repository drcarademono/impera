"""Asset checks must be independent of the host's PNG compressor."""
import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib


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
        self.png = self.root / 'textures/tilesets/Ultima_5_Tiles_Zhelkas.png'

    def check_assets(self, compression=9):
        code = (
            'import runpy, sys, zlib; original = zlib.compress; '
            f'zlib.compress = lambda data, level=9: original(data, {compression}); '
            "sys.argv = ['scripts/embed-tilesets.py', '--check']; "
            "runpy.run_path(sys.argv[0], run_name='__main__')"
        )
        return subprocess.run([sys.executable, '-c', code], cwd=self.root,
                              capture_output=True, text=True)

    def test_different_compression_preserves_pixels_and_files(self):
        original = self.png.read_bytes()
        for compression in (0, 1, 9):
            with self.subTest(compression=compression):
                result = self.check_assets(compression)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertEqual(self.png.read_bytes(), original)

    def test_changed_pixel_is_still_rejected(self):
        data = self.png.read_bytes()
        offset, chunks, compressed = 8, [], bytearray()
        while offset < len(data):
            size = struct.unpack_from('>I', data, offset)[0]
            name = data[offset+4:offset+8]
            payload = data[offset+8:offset+8+size]
            if name == b'IDAT':
                compressed.extend(payload)
            else:
                chunks.append((name, payload))
            offset += size + 12
        pixels = bytearray(zlib.decompress(compressed))
        pixels[1] ^= 1  # One actual color component, with valid PNG checksums.
        chunks.insert(1, (b'IDAT', zlib.compress(pixels, 9)))
        out = bytearray(data[:8])
        for name, payload in chunks:
            out.extend(struct.pack('>I', len(payload)) + name + payload +
                       struct.pack('>I', zlib.crc32(name + payload)))
        self.png.write_bytes(out)
        result = self.check_assets()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Zhelkas PNG is stale', result.stderr)

    def test_corrupt_png_is_rejected(self):
        data = bytearray(self.png.read_bytes())
        data[29] ^= 1  # IHDR checksum.
        self.png.write_bytes(data)
        result = self.check_assets()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Zhelkas PNG is stale', result.stderr)

    def test_stale_embedded_bytes_are_still_rejected(self):
        header = self.root / 'src/graphics/tileset_assets.h'
        header.write_text(header.read_text() + '// stale\n')
        result = self.check_assets()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Tileset bytes are stale', result.stderr)


if __name__ == '__main__':
    unittest.main()
