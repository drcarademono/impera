import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('extract_graphics', ROOT / 'scripts/extract-graphics.py')
extractor = importlib.util.module_from_spec(spec)
spec.loader.exec_module(extractor)

class GraphicsExportTest(unittest.TestCase):
    def test_all_original_resources_and_png_integrity(self):
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder)
            count = extractor.extract(ROOT / 'Ultima 5', output)
            entries = json.loads((output / 'manifest.json').read_text())
            self.assertEqual(count, len(entries))
            sources = {e['source'].lower() for e in entries}
            expected = {p.name.lower() for p in (ROOT / 'Ultima 5').iterdir()
                        if p.suffix.lower() in ('.16', '.4', '.bit', '.ch', '.hcs')}
            self.assertEqual(sources, expected)
            self.assertTrue((output / 'tiles-16/sprites/tile-144.png').exists())
            for entry in entries:
                data = (output / entry['file']).read_bytes()
                self.assertEqual(data[:8], b'\x89PNG\r\n\x1a\n')
                position = 8
                compressed = b''
                while position < len(data):
                    length, = struct.unpack_from('>I', data, position)
                    kind = data[position+4:position+8]
                    payload = data[position+8:position+8+length]
                    crc, = struct.unpack_from('>I', data, position+8+length)
                    self.assertEqual(crc, zlib.crc32(kind+payload) & 0xffffffff)
                    if kind == b'IHDR':
                        self.assertEqual(struct.unpack_from('>II',payload), (entry['width'],entry['height']))
                    if kind == b'IDAT': compressed += payload
                    position += length+12
                self.assertEqual(len(zlib.decompress(compressed)),entry['height']*(1+4*entry['width']))
            original = (output / 'tiles-16/sprites/tile-144.png').read_bytes()
            with self.assertRaises(FileExistsError): extractor.extract(ROOT / 'Ultima 5',output)
            self.assertEqual((output / 'tiles-16/sprites/tile-144.png').read_bytes(),original)

    def test_truncated_lzw_rejected(self):
        with self.assertRaises(ValueError): extractor.decompress(struct.pack('<I',100)+b'\0')

if __name__ == '__main__': unittest.main()
