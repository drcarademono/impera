import hashlib
import json
import tempfile
import unittest
from pathlib import Path

from PIL import Image, ImageChops

from tools.city_design import CityMap
from tools.city_design.art import render
from tools.city_design.layout import analyze, reachable
from tools.city_design.model import _metadata
from tools.city_design.experiments.minoc_u7.design import (
    ARRIVAL, BRIDGES, EXTRA_DORMS, FLOORS, LADDERS, SOURCE, SOURCE_SHA256,
    TARGETS, final_footprints, hydrology, rect_cells, schedule_audit,
    water_components, write_study,
)

STUDY = Path(__file__).resolve().parents[1]/'experiments/minoc_u7/study'


class UltimaVIIStudyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.city = CityMap.load(STUDY/'04-final.json')

    def test_fresh_dimensions_and_exact_original_metadata(self):
        source = CityMap.load(SOURCE)
        self.assertEqual(hashlib.sha256(SOURCE.read_bytes()).hexdigest(), SOURCE_SHA256)
        self.assertEqual(_metadata(self.city.data), _metadata(source.data))
        self.assertEqual(len(self.city.data['map']['npcs']), 14)
        for f in self.city.data['map']['floors']:
            self.assertEqual((f['width'], f['height']), (64, 64))
        self.assertEqual(self.city.floor(0)['gates'], [])

    def test_no_town_wall_or_unintended_outdoor_flooring(self):
        footprints = set().union(*final_footprints().values())
        pier = rect_cells((4, 57, 3, 4)) | rect_cells((6, 59, 5, 2))
        for y, row in enumerate(self.city.floor(0)['tiles']):
            for x, tile in enumerate(row):
                if tile in (0x4d, 0x4f):
                    self.assertIn((x, y), footprints)
                if tile in FLOORS:
                    self.assertIn((x, y), footprints | pier)
                if x in (0, 63) or y in (0, 63):
                    self.assertNotIn(tile, (0x4d, 0x4f))

    def test_continuous_river_separate_bay_and_correct_bridges(self):
        sea, river = hydrology(True)
        self.assertFalse(sea & river)
        self.assertEqual(len(water_components(river)), 1)
        self.assertTrue(any(y == 63 for x, y in river))
        self.assertTrue(any(x == 0 for x, y in sea))
        self.assertTrue(any(y == 63 for x, y in sea))
        self.assertTrue(BRIDGES <= river)
        footprints = set().union(*final_footprints().values())
        self.assertFalse(river & footprints)
        for x, y in river:
            self.assertEqual(self.city.floor(0)['tiles'][y][x], 0x6a if (x, y) in BRIDGES else 2)

    def test_worker_beds_paired_and_rough_walled(self):
        footprints = final_footprints()
        names = ['north_dormitory', 'south_dormitory', *EXTRA_DORMS]
        count = 0
        for z in (0, 1):
            for name in names:
                if z == 1 and name not in EXTRA_DORMS:
                    continue
                cells = footprints[name]
                tiles = self.city.floor(z)['tiles']
                for x, y in cells:
                    if tiles[y][x] == 0xab:
                        self.assertIn((x+1, y), cells)
                        self.assertEqual(tiles[y][x+1], 0xac)
                        count += 1
                    if tiles[y][x] == 0xac:
                        self.assertEqual(tiles[y][x-1], 0xab)
                    self.assertNotEqual(tiles[y][x], 0x4f)
        self.assertEqual(count, 62)

    def test_original_schedules_and_contents_have_usable_new_spaces(self):
        self.assertEqual(schedule_audit(self.city), [])
        landings = set()
        for npc in self.city.data['map']['npcs']:
            s = npc['schedule']
            landings.update((s[3+i], s[6+i]) for i in range(3) if s[9+i] == 1)
        upper_access = set().union(*(reachable(self.city, 1, p, doors=True) for p in LADDERS))
        self.assertTrue(landings <= upper_access)
        for c in self.city.floor(1)['contents']:
            self.assertEqual(self.city.floor(1)['tiles'][c['y']][c['x']], 0xaf)
        for x, y in LADDERS:
            self.assertEqual(self.city.floor(0)['tiles'][y][x], 0xc8)
            self.assertEqual(self.city.floor(1)['tiles'][y][x], 0xc9)

    def test_paths_and_all_intended_work_areas_accessible_conditionally(self):
        report = analyze(self.city, 0, ARRIVAL, TARGETS)
        self.assertFalse(report['errors'])
        self.assertFalse(any('opposite' in str(w) for w in report['warnings']))
        self.assertEqual(report['targets'][-1]['status'], 'reachable')
        for z in (0, 1):
            self.assertFalse(any('ladder' in str(w).lower() for w in analyze(self.city, z)['warnings']))

    def test_previews_and_replay_match_saved_outputs(self):
        for z, label in ((0, 'ground'), (1, 'upper')):
            actual = Image.open(STUDY/f'04-final-{label}.png').convert('RGB')
            self.assertIsNone(ImageChops.difference(actual, render(self.city, z)).getbbox())
        before = SOURCE.read_bytes()
        with tempfile.TemporaryDirectory() as td:
            output = Path(td)/'replay'
            write_study(output)
            for p in STUDY.glob('[0-9][0-9]-*.json'):
                self.assertEqual((output/p.name).read_bytes(), p.read_bytes())
            with self.assertRaises(FileExistsError):
                write_study(output)
        self.assertEqual(SOURCE.read_bytes(), before)


if __name__ == '__main__':
    unittest.main()
