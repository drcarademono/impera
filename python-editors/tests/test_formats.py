import random
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

EDITORS = Path(__file__).resolve().parents[1]
REPO = EDITORS.parent
sys.path.insert(0, str(EDITORS))
from u5_formats import lzw_decode, lzw_encode, read_tiles_blob, tlk_header, build_tlk, write_map_pair
import add_tails_tlk as tails

class FormatsTest(unittest.TestCase):
    def test_lzw_boundaries_and_resets(self):
        for length in [0, 1, 254, 255, 256, 257, 512, 2048, 65536, 150000]:
            for raw in [random.Random(length).randbytes(length), b'A' * length]:
                with self.subTest(length=length, repeated=raw[:10] == b'A'*10):
                    self.assertEqual(lzw_decode(lzw_encode(raw), length), raw)

    def test_lzw_rejects_invalid_header_and_truncated_stream(self):
        for comp, size in [(b'', 1), (b'\xff\x01', 1), (lzw_encode(b'a'), 2), (b'', 8388609)]:
            with self.subTest(size=size), self.assertRaises(ValueError):
                lzw_decode(comp, size)

    def test_raw_tiles_not_confused_with_header(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d)/'TILES.16'
            raw = struct.pack('<I', 65536) + bytes(65532)
            p.write_bytes(raw)
            self.assertEqual(read_tiles_blob(p), raw)
            p.write_bytes(struct.pack('<I', 65536)+lzw_encode(raw))
            self.assertEqual(read_tiles_blob(p), raw)

    @unittest.skipUnless(shutil.which('cc'), 'A C compiler is required for native decoder verification')
    def test_encoder_with_actual_engine_decoder(self):
        with tempfile.TemporaryDirectory() as d:
            d = Path(d)
            exe = d/'decoder'
            subprocess.run(['cc','-std=c99','-I',str(REPO/'src'), str(REPO/'src/common/lzw.c'),
                            str(EDITORS/'tests/lzw_harness.c'),'-o',str(exe)], check=True)
            for length in [1, 254, 255, 256, 257, 512, 4096, 65536, 150000]:
                raw = random.Random(length).randbytes(length)
                source, target = d/'resource.16', d/'decoded'
                source.write_bytes(struct.pack('<I',len(raw))+lzw_encode(raw))
                subprocess.run([str(exe),str(source),str(target)], check=True)
                self.assertEqual(target.read_bytes(), raw)

    def test_tlk_rejects_bad_offsets_and_duplicate_ids(self):
        for blob in [b'', b'\x01\x00', struct.pack('<HHH',1,1,2)+b'a',
                     struct.pack('<HHH',1,1,99)+b'a',
                     struct.pack('<HHHHH',2,1,10,1,11)+b'ab',
                     struct.pack('<HHHHH',2,1,11,2,10)+b'ab']:
            with self.subTest(blob=blob), self.assertRaises(ValueError):
                tlk_header(blob)

    def test_tails_idempotent_with_padding(self):
        segments = [b'one\0'+tails.parse_tail_hex('90 9f c0'),
                    b'two\0'+tails.parse_tail_hex('909fc0')+b'\0\0', b'three\0']
        once = tails.add_tails_to_segments(segments, b'\x90\x9f\xc0', False)
        self.assertEqual(once, tails.add_tails_to_segments(once, b'\x90\x9f\xc0', False))
        self.assertEqual(once[:2], segments[:2])

    def test_tlk_offset_overflow_rejected(self):
        with self.assertRaises(ValueError):
            build_tlk([(i,0) for i in range(34)], [b'x'*1024]*34)
        with self.assertRaises(ValueError):
            tails.rebuild_tlk(2, [(1,0)], [b'x'])

    def test_paired_map_save_rolls_back(self):
        from unittest.mock import patch
        with tempfile.TemporaryDirectory() as d:
            a,b = Path(d)/'BRIT.DAT', Path(d)/'DATA.OVL'
            a.write_bytes(b'old map'); b.write_bytes(b'old index')
            replace = Path.replace
            def fail_index(source, target):
                if target == b:
                    raise OSError('simulated replacement failure')
                return replace(source, target)
            with patch.object(Path,'replace',fail_index), self.assertRaises(OSError):
                write_map_pair(a,b'new map',b,b'new index')
            self.assertEqual(a.read_bytes(), b'old map')
            self.assertEqual(b.read_bytes(), b'old index')
            self.assertEqual(sorted(p.name for p in Path(d).iterdir()), ['BRIT.DAT','DATA.OVL'])

if __name__ == '__main__':
    unittest.main()
