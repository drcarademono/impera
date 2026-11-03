#!/usr/bin/env python3
"""The checked-in catalog must be independent of filesystem enumeration order."""
import pathlib
import random
import runpy
import sys
import unittest
from unittest.mock import patch

ROOT = pathlib.Path(__file__).resolve().parents[1]
GENERATOR = ROOT / 'scripts/generate-audio-catalog.py'
CATALOG = ROOT / 'src/audio/audio_catalog.h'


class AudioCatalogTest(unittest.TestCase):
    def test_directory_order_does_not_change_catalog(self):
        original_glob = pathlib.Path.glob
        expected = CATALOG.read_text(encoding='utf-8')
        for order in ('ascending', 'descending', 'shuffled'):
            with self.subTest(order=order):
                generated = []

                def enumerate_files(directory, pattern):
                    paths = sorted(original_glob(directory, pattern))
                    if order == 'descending':
                        paths.reverse()
                    elif order == 'shuffled':
                        random.Random(42).shuffle(paths)
                    return iter(paths)

                def capture_write(path, text, **kwargs):
                    self.assertEqual(path, CATALOG)
                    self.assertEqual(kwargs, {'encoding': 'utf-8', 'newline': '\n'})
                    generated.append(text)
                    return len(text)

                with patch.object(pathlib.Path, 'glob', enumerate_files), \
                     patch.object(pathlib.Path, 'write_text', capture_write), \
                     patch.object(sys, 'argv', [str(GENERATOR)]):
                    result = runpy.run_path(str(GENERATOR))
                self.assertEqual(len(result['rules']), 115)
                self.assertEqual(generated, [expected])


if __name__ == '__main__':
    unittest.main()
