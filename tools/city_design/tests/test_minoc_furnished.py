import hashlib
import json
import tempfile
import unittest
from collections import Counter
from pathlib import Path

from PIL import Image, ImageChops

from tools.city_design import CityMap
from tools.city_design.art import render
from tools.city_design.layout import analyze, reachable
from tools.city_design.model import _metadata
from tools.city_design.experiments.minoc_u7.design import (
    SOURCE, SOURCE_SHA256, LADDERS, hydrology, schedule_audit, line,
)
from tools.city_design.experiments.minoc_furnished.design import (
    BASE, BASE_SHA256, FACTORY, DIRT_SPURS, eight, write_study,
)

ROOT=Path(__file__).resolve().parents[1]
STUDY=ROOT/'experiments/minoc_furnished/study'


class FurnishedMinocTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.city=CityMap.load(STUDY/'03-final.json')

    def test_metadata_and_old_studies_preserved(self):
        self.assertEqual(hashlib.sha256(SOURCE.read_bytes()).hexdigest(),SOURCE_SHA256)
        self.assertEqual(hashlib.sha256(BASE.read_bytes()).hexdigest(),BASE_SHA256)
        self.assertEqual(_metadata(self.city.data),_metadata(CityMap.load(SOURCE).data))
        self.assertEqual(self.city.floor(0)['gates'],[])

    def test_no_diagonal_floor_grass_pinhole_at_factory_corners(self):
        for z in (0,1):
            for x,y in FACTORY:
                if not eight(x,y)<=FACTORY:
                    self.assertNotIn(self.city.floor(z)['tiles'][y][x],(0x40,0x44,0x45))

    def test_factory_hall_and_all_eight_rooms_accessible(self):
        targets=[('hall',29,26),('NW',27,20),('NE',31,19),('WU',23,23),('WL',23,28),
                 ('EU',36,23),('EL',36,28),('SW',28,31),('SE',32,31)]
        report=analyze(self.city,start=(32,62),targets=targets)
        self.assertFalse(report['errors'])
        self.assertFalse(any('opposite' in str(w) for w in report['warnings']))

    def test_furniture_is_varied_and_complete(self):
        counts=Counter(t for f in self.city.data['map']['floors'] for row in f['tiles'] for t in row)
        self.assertEqual(sum(counts[t] for t in range(0x90,0x94)),80)
        self.assertGreaterEqual(counts[0xad],20)
        self.assertGreaterEqual(counts[0xae],20)
        self.assertGreaterEqual(counts[0x9d],10)
        for f in self.city.data['map']['floors']:
            for y,row in enumerate(f['tiles']):
                for x,t in enumerate(row):
                    if t==0xab:self.assertEqual(row[x+1],0xac)
                    if t==0xac:self.assertEqual(row[x-1],0xab)
        base=CityMap.load(BASE)
        self.assertEqual(counts[0xab],sum(t==0xab for f in base.data['map']['floors']
                                        for row in f['tiles'] for t in row))

    def test_main_roads_paved_dormitory_spurs_dirt(self):
        g=self.city.floor(0)['tiles']
        allowed=set().union(*(line([a,b]) for points in DIRT_SPURS for a,b in zip(points,points[1:])))
        actual={(x,y) for y,row in enumerate(g) for x,t in enumerate(row) if 0x20<=t<=0x26}
        self.assertEqual(actual,allowed)
        for x,y in ((39,34),(12,34),(20,26),(25,42),(58,49)):
            self.assertEqual(g[y][x],0x44)

    def test_shore_has_no_desert_and_river_preserved(self):
        for f in self.city.data['map']['floors']:
            self.assertFalse(any(7 in row for row in f['tiles']))
        _,river=hydrology(True)
        before=CityMap.load(BASE).floor(0)['tiles']
        for x,y in river:
            self.assertEqual(self.city.floor(0)['tiles'][y][x],before[y][x])

    def test_original_destinations_and_loft_routes_remain_passable(self):
        self.assertEqual(schedule_audit(self.city),[])
        upper=set().union(*(reachable(self.city,1,p,doors=True) for p in LADDERS))
        for npc in self.city.data['map']['npcs']:
            s=npc['schedule']
            for i in range(3):
                if s[9+i]==1:self.assertIn((s[3+i],s[6+i]),upper)

    def test_native_style_evidence_covers_all_locations_and_slots(self):
        evidence=json.loads((ROOT/'style_evidence/survey.json').read_text())
        self.assertEqual(len(evidence['locations']),32)
        self.assertEqual(len(evidence['catalogueDescriptions']),256)
        self.assertEqual(sum(evidence['tileCounts'][str(t)] for t in range(0x90,0x94)),308)
        self.assertEqual(evidence['chairAdjacentTableDirections']['0x90']['N'],107)
        self.assertEqual(evidence['chairAdjacentTableDirections']['0x92']['S'],65)
        self.assertEqual(Image.open(ROOT/'style_evidence/all-512-dos-slots.png').size,(1152,2880))

    def test_render_and_replay(self):
        for z,label in ((0,'ground'),(1,'upper')):
            image=Image.open(STUDY/f'03-final-{label}.png').convert('RGB')
            self.assertIsNone(ImageChops.difference(image,render(self.city,z)).getbbox())
        with tempfile.TemporaryDirectory() as td:
            output=Path(td)/'new-study';write_study(output)
            for p in STUDY.glob('[0-9][0-9]-*.json'):
                self.assertEqual((output/p.name).read_bytes(),p.read_bytes())
            with self.assertRaises(FileExistsError):write_study(output)


if __name__=='__main__':unittest.main()
