import copy
import hashlib
import tempfile
import unittest
from pathlib import Path

from PIL import Image, ImageChops

from tools.city_design import CityMap
from tools.city_design.art import render
from tools.city_design.experiments.minoc.design import (SOURCE, SOURCE_SHA256, NEW_START, TARGETS,
                                                       authorized_wall_cells, perimeter_points, write_study)
from tools.city_design.layout import analyze
from tools.city_design.model import _metadata

STUDY = Path(__file__).resolve().parents[1] / 'experiments/minoc/study'


class MinocStudyTests(unittest.TestCase):
    def test_authoritative_input_and_all_original_content_preserved(self):
        self.assertEqual(hashlib.sha256(SOURCE.read_bytes()).hexdigest(), SOURCE_SHA256)
        source = CityMap.load(SOURCE)
        self.assertEqual(len(source.data['map']['npcs']), 14)
        paths = sorted(STUDY.glob('[0-9][0-9]-*.json'))
        self.assertEqual(len(paths), 6)
        for path in paths:
            city = CityMap.load(path)
            original_metadata, edited_metadata = _metadata(source.data), _metadata(city.data)
            if path.stem == '06-final':
                original_metadata['map']['floors'][0]['gates'] = edited_metadata['map']['floors'][0]['gates']
            self.assertEqual(original_metadata, edited_metadata)
            for z in (0, 1):
                floor = city.floor(z)
                self.assertEqual((floor['width'], floor['height']), (64, 64))
                allowed = set(authorized_wall_cells(source, z)) if path.stem == '06-final' else set()
                for y in range(32):
                    for x in range(32):
                        if (x, y) not in allowed:
                            self.assertEqual(floor['tiles'][y][x], source.floor(z)['tiles'][y][x])

    def test_final_previews_match_actual_embedded_art(self):
        city = CityMap.load(STUDY / '06-final.json')
        for z, label in ((0, 'ground'), (1, 'upper')):
            expected = render(city, z)
            with Image.open(STUDY / f'06-final-{label}.png') as actual:
                self.assertEqual(actual.size, (1024, 1024))
                self.assertIsNone(ImageChops.difference(actual.convert('RGB'), expected).getbbox())

    def test_final_target_connectivity_and_ladders(self):
        city = CityMap.load(STUDY / '06-final.json')
        report = analyze(city, start=NEW_START, targets=TARGETS)
        self.assertFalse(report['errors'])
        self.assertEqual(sum(t['status'] == 'door-dependent' for t in report['targets']), 3)
        for z in (0, 1):
            self.assertFalse(any('ladder' in str(w).lower() or 'stair' in str(w).lower()
                                 for w in analyze(city, z)['warnings']))

    def test_recipe_replays_identical_maps_without_touching_input(self):
        before = SOURCE.read_bytes()
        with tempfile.TemporaryDirectory() as td:
            output = Path(td) / 'new-study'
            write_study(output)
            for path in STUDY.glob('[0-9][0-9]-*.json'):
                self.assertEqual((output / path.name).read_bytes(), path.read_bytes())
            with self.assertRaises(FileExistsError):
                write_study(output)
        self.assertEqual(SOURCE.read_bytes(), before)

    def test_final_streets_do_not_repeat_directional_dirt_components(self):
        city = CityMap.load(STUDY / '06-final.json')
        self.assertFalse(any(0x20 <= tile <= 0x26 for row in city.floor(0)['tiles'] for tile in row))

    def test_perimeter_and_gate_do_not_cut_buildings(self):
        city = CityMap.load(STUDY / '06-final.json')
        previous = CityMap.load(STUDY / '05-paved.json')
        for x, y, w, h in ((40, 18, 14, 12), (23, 44, 11, 9), (43, 43, 14, 10)):
            for row in range(y, y + h):
                self.assertEqual(city.floor(0)['tiles'][row][x:x+w], previous.floor(0)['tiles'][row][x:x+w])
        self.assertGreater(len(perimeter_points()), 8)
        for (ax, ay), (bx, by) in zip(perimeter_points(), perimeter_points()[1:]):
            for y in range(min(ay, by), max(ay, by)+1):
                for x in range(min(ax, bx), max(ax, bx)+1):
                    self.assertEqual(city.floor(0)['tiles'][y][x], 0x87 if (x, y) in ((31, 61), (32, 61)) else 0x4f)
        gates = city.floor(0)['gates']
        self.assertEqual([g[2:4] for g in gates], [[31, 62], [32, 62]])
        self.assertTrue(all(g[5] == 2 for g in gates))


if __name__ == '__main__':
    unittest.main()
