import base64
import copy
import json
import tempfile
import unittest
from pathlib import Path

from PIL import Image

from tools.city_design import CityMap, MapError
from tools.city_design.art import PALETTE, DosArt, decode_tiles, decompress, render, compare
from tools.city_design.catalogue import build_catalogue, load_catalogue
from tools.city_design.layout import analyze, movement
from tools.city_design.patterns import building, house, inn, shop, workshop
from tools.city_design.paths import dirt_path


def fixture():
    # Synthetic pixels and metadata, never a substitute for the Minoc export.
    f = {'floor': 0, 'width': 8, 'height': 8, 'background': 5,
         'tiles': [[5] * 8 for _ in range(8)], 'contents': [], 'signs': [], 'gates': []}
    return {'format': 'impera-map', 'version': 1,
            'map': {'kind': 'location', 'name': 'TEST ONLY', 'floors': [f],
                    'npcs': [], 'dialogues': {}, 'unknownFutureMetadata': {'keep': [1, 2]}},
            'customTypes': [], 'vanillaTypes': [],
            'artwork': {'dos': base64.b64encode(bytes([0x12]) * 65536).decode(),
                        'slots': [5], 'sheets': []}}


class ModelTests(unittest.TestCase):
    def setUp(self):
        self.city = CityMap(fixture())

    def test_expand_preserves_every_original_cell_and_metadata(self):
        self.city.protect_original()
        self.city.resize(0, 64, 64, 6)
        self.assertEqual(self.city.floor(0)['tiles'][7][:8], [5] * 8)
        self.assertEqual(self.city.floor(0)['tiles'][8], [6] * 64)
        self.assertEqual(self.city.preservation(), [])
        self.assertIn(6, self.city.data['artwork']['slots'])

    def test_multi_floor_preservation(self):
        d = fixture()
        d['map']['floors'].append(dict(copy.deepcopy(d['map']['floors'][0]), floor=1))
        city = CityMap(d)
        city.protect_original()
        for z in (0, 1):
            city.resize(z, 64, 64, 5)
        self.assertEqual(city.preservation(), [])
        self.assertEqual(city.floor(1)['tiles'][0][:8], [5] * 8)

    def test_atomic_protected_paint(self):
        self.city.protect(0, (1, 1, 2, 2))
        before = copy.deepcopy(self.city.data)
        with self.assertRaises(MapError):
            self.city.paint(0, [(0, 0, 6), (1, 1, 7)])
        self.assertEqual(before, self.city.data)

    def test_protection_detects_direct_mutation(self):
        self.city.protect_original()
        self.city.data['map']['floors'][0]['tiles'][0][0] = 8
        self.assertTrue(self.city.preservation())

    def test_protect_new_area(self):
        self.city.resize(0, 16, 16, 6)
        self.city.protect(0, (10, 10, 2, 2))
        self.city.floor(0)['tiles'][10][10] = 5
        self.assertTrue(self.city.preservation())

    def test_unknown_metadata_is_not_discarded(self):
        self.city.data['map']['unknownFutureMetadata']['keep'].append(3)
        self.assertTrue(self.city.preservation())

    def test_authorized_exceptions_are_narrow_and_exact(self):
        self.city.protect_original()
        self.city.authorize_terrain(0, [(1, 1)], 'Explicit test authorization')
        self.city.paint(0, [(1, 1, 0x4f)])
        self.assertFalse(self.city.preservation())
        with self.assertRaises(MapError):
            self.city.paint(0, [(2, 1, 0x4f)])
        self.city.authorize_metadata(('map', 'floors', 0, 'partyStart'), [3, 4], 'Explicit start authorization')
        self.city.floor(0)['partyStart'] = [3, 4]
        self.assertFalse(self.city.preservation())
        self.city.floor(0)['partyStart'] = [4, 4]
        self.assertTrue(self.city.preservation())

    def test_source_and_existing_outputs_cannot_be_overwritten(self):
        with tempfile.TemporaryDirectory() as td:
            source = Path(td) / 'source.json'
            self.city.save(source)
            city = CityMap.load(source)
            before = source.read_bytes()
            with self.assertRaises(MapError):
                city.save(source)
            with self.assertRaises(FileExistsError):
                self.city.save(source)
            self.assertEqual(source.read_bytes(), before)

    def test_export_roundtrip(self):
        self.city.resize(0, 16, 16, 5)
        self.city.rectangle(0, (10, 10, 2, 2), 0x44)
        with tempfile.TemporaryDirectory() as td:
            path = Path(td) / 'experiment.json'
            self.city.save(path)
            self.assertEqual(CityMap.load(path).data, self.city.data)
        self.assertIn(0x44, self.city.data['artwork']['slots'])

    def test_reject_invalid_dimensions_and_rows(self):
        for key, value in [('width', True), ('height', 257), ('floor', 4), ('width', 7)]:
            d = fixture()
            d['map']['floors'][0][key] = value
            with self.assertRaises(MapError):
                CityMap(d)

    def test_reject_duplicate_floor_and_missing_type(self):
        d = fixture()
        d['map']['floors'] *= 2
        with self.assertRaises(MapError):
            CityMap(d)
        d = fixture()
        d['map']['floors'][0]['tiles'][0][0] = 600
        with self.assertRaises(MapError):
            CityMap(d)

    def test_crop_cannot_strand_schedule(self):
        d = fixture()
        d['map']['npcs'] = [{'slot': 1, 'appearance': 72, 'conversation': 1,
                              'schedule': [0, 1, 0, 7, 7, 7, 7, 7, 7, 0, 0, 0, 0, 6, 12, 18]}]
        city = CityMap(d)
        before = copy.deepcopy(city.data)
        with self.assertRaises(MapError):
            city.resize(0, 6, 6, 5)
        self.assertEqual(city.data, before)

    def test_crop_cannot_strand_sign_or_gate(self):
        for field, value in [('signs', [{'x': 7, 'y': 7, 'text': 'keep'}]),
                             ('gates', [[0, 0, 7, 7] + [0] * 12])]:
            d = fixture()
            d['map']['floors'][0][field] = value
            city = CityMap(d)
            with self.assertRaises(MapError):
                city.resize(0, 4, 4, 5)

    def test_duplicate_keys_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            p = Path(td) / 'bad.json'
            p.write_text('{"format":"impera-map","format":"other"}')
            with self.assertRaises(MapError):
                CityMap.load(p)

    def test_irregular_stamp_is_atomic(self):
        self.city.stamp(0, 1, 1, [[6, None], [7, 8]])
        self.assertEqual(self.city.floor(0)['tiles'][1][2], 5)
        before = copy.deepcopy(self.city.data)
        with self.assertRaises(MapError):
            self.city.stamp(0, 7, 7, [[6, 7]])
        self.assertEqual(before, self.city.data)


class RenderingTests(unittest.TestCase):
    def test_nibbles_palette_and_tile_boundaries(self):
        raw = bytearray(65536)
        raw[0] = 0x1e
        raw[128] = 0xf2
        tiles = decode_tiles(raw)
        self.assertEqual(tiles[0].getpixel((0, 0)), PALETTE[1])
        self.assertEqual(tiles[0].getpixel((1, 0)), PALETTE[14])
        self.assertEqual(tiles[1].getpixel((0, 0)), PALETTE[15])

    def test_lzw_reset_width_growth_and_special_dictionary_entry(self):
        # Independent literal-only writer forces resets and width boundaries.
        data = bytes(range(256)) * 256
        codes, width, next_code, previous, bits, bit_count = [], 9, 258, False, 0, 0
        encoded = bytearray((65536).to_bytes(4, 'little'))
        for code in [256] + list(data[:2000]) + [256] + list(data[2000:]):
            bits |= code << bit_count
            bit_count += width
            while bit_count >= 8:
                encoded.append(bits & 255)
                bits >>= 8
                bit_count -= 8
            if code == 256:
                width, next_code, previous = 9, 258, False
            else:
                if previous and next_code < 4096:
                    next_code += 1
                    if next_code == 1 << width and width < 12:
                        width += 1
                previous = True
        if bit_count:
            encoded.append(bits & 255)
        self.assertEqual(decompress(bytes(encoded)), data)
        # Code 258 before it exists is the standard previous+first-byte case.
        bits = 256 | (65 << 9) | (258 << 18)
        special = (3).to_bytes(4, 'little') + bits.to_bytes(4, 'little')
        self.assertEqual(decompress(special, expected=3), b'AAA')

    def test_truncated_and_malformed_resources_rejected(self):
        for data in (b'', b'\x00\x00\x01\x00', bytes(100)):
            with self.assertRaises(ValueError):
                decode_tiles(data)

    def test_region_nearest_neighbor_and_comparison(self):
        city = CityMap(fixture())
        image = render(city, region=(1, 1, 2, 3), scale=3)
        self.assertEqual(image.size, (96, 144))
        self.assertEqual(image.getpixel((0, 0)), image.getpixel((2, 2)))
        self.assertNotEqual(image.getpixel((2, 0)), image.getpixel((3, 0)))
        after = CityMap(city.data)
        after.paint(0, [(0, 0, 6)])
        self.assertEqual(compare(city, after).getpixel((144, 0)), (255, 151, 0))

    def test_custom_artwork_fails_explicitly(self):
        city = CityMap(fixture())
        art = DosArt(city.data)
        with self.assertRaisesRegex(ValueError, 'No substitute'):
            art.tile(600)
        record = bytearray(256)
        record[248:250] = (1).to_bytes(2, 'little')
        art.overrides[5] = bytes(record)
        with self.assertRaisesRegex(ValueError, 'No substitute'):
            art.tile(5)


class ArchitectureTests(unittest.TestCase):
    def test_directional_path_matches_bends_and_straights(self):
        city = CityMap(fixture())
        dirt_path(city, 0, [(1, 1), (1, 4), (5, 4)])
        tiles = city.floor(0)['tiles']
        self.assertEqual(tiles[2][1], 0x20)
        self.assertEqual(tiles[4][3], 0x21)
        self.assertEqual(tiles[4][1], 0x22)  # North + east.
        dirt_path(city, 0, [(3, 4), (3, 6)])
        self.assertEqual(tiles[4][3], 0x26)

    def test_catalogue_matches_audit_and_native_tables(self):
        self.assertEqual(load_catalogue(), build_catalogue())
        tiles = {r['id']: r for r in load_catalogue()['tiles']}
        self.assertEqual(tiles[0x58]['description'], 'Anvil')
        self.assertFalse(tiles[0x4f]['partyWalkable'])
        self.assertTrue(tiles[5]['partyWalkable'])
        self.assertIn('roof', tiles[0x27]['roles'])

    def test_patterns_have_accessible_interior_and_different_footprints(self):
        for factory in (house, shop, workshop, inn):
            city = CityMap(fixture())
            city.resize(0, 24, 24, 5)
            city.stamp(0, 2, 2, factory())
            report = analyze(city, start=(0, 0), targets=[('inside', 5, 5)])
            self.assertFalse(report['errors'])
            self.assertEqual(report['targets'][0]['status'], 'door-dependent')
        rows = building(10, 10, wing=(0, 0, 3, 3))
        self.assertIsNone(rows[0][0])
        with self.assertRaises(MapError):
            building(7, 7, door=(3, 3))

    def test_blocked_entrance_and_isolated_interior(self):
        city = CityMap(fixture())
        city.stamp(0, 1, 1, building(6, 6, windows=False))
        city.paint(0, [(4, 7, 0x4f)])
        report = analyze(city, start=(0, 0), targets=[('inside', 3, 3)])
        self.assertTrue(report['errors'])
        self.assertTrue(any('Entrance at' in w for w in report['warnings']))

    def test_stairs_warn_instead_of_inventing_vertical_passage(self):
        city = CityMap(fixture())
        city.paint(0, [(1, 1, 0xc8), (2, 2, 0xc4)])
        report = analyze(city)
        self.assertTrue(any('no destination' in w for w in report['warnings']))
        self.assertTrue(any('approach-direction' in w for w in report['warnings']))

    def test_custom_movement_overrides_native(self):
        d = fixture()
        raw = bytearray(256)
        raw[:4] = (256).to_bytes(4, 'little')
        raw[4:8] = (5).to_bytes(4, 'little')
        d['vanillaTypes'] = [{'id': 5, 'record': base64.b64encode(raw).decode()}]
        self.assertFalse(movement(CityMap(d))(5))


if __name__ == '__main__':
    unittest.main()
