"""A furnished Minoc revision with native wall joints and reference factory plan."""
import argparse
import hashlib
import json
from pathlib import Path

from ...art import render, compare
from ...layout import analyze, movement
from ...model import CityMap, MapError
from ...paths import dirt_path
from ..minoc_u7.design import (
    ARRIVAL, AUTHORIZATION, SOURCE, SOURCE_SHA256, FLOORS, BRIDGES, LADDERS,
    final_footprints, initial, coastal_revision, river_revision, integration_revision,
    polygon, rect_cells, neighbors, schedule_audit, hydrology, water_components,
)

BASE = Path(__file__).resolve().parents[1]/'minoc_u7/study/04-final.json'
BASE_SHA256 = '71dbe506fb42b6d0d3da353be5927f640876c9718c8a64ae1c7dffc34a47a035'
ROOMS = {
    'northwest': (25,17,5,5), 'northeast': (30,17,5,5),
    'west_upper': (21,21,5,5), 'west_lower': (21,26,5,5),
    'east_upper': (34,21,5,5), 'east_lower': (34,26,5,5),
    'southwest': (25,29,5,5), 'southeast': (30,29,5,5),
}
FACTORY = (set().union(*(rect_cells(r) for r in ROOMS.values())) |
           polygon([(27,21),(32,21),(34,23),(34,27),(32,29),(27,29),(25,27),(25,23)]) |
           rect_cells((28,20,4,4)) | rect_cells((28,27,4,4)) |
           rect_cells((24,23,4,5)) | rect_cells((31,23,4,5)))
ENTRANCES = {(26,17),(21,28),(38,23),(30,33)}
DIRT_SPURS = [[(9,14),(11,14)],[(7,31),(7,33)],[(26,52),(31,52),(31,51)],
              [(36,61),(36,62),(33,62)]]
WORK_TARGETS = [('factory hall',29,26),('west workshop',23,23),('east workshop',36,23),
                ('south stores',32,31),('river factory',46,45),('east-bank factory',58,55),
                ('shipbuilder',6,53),('north dormitory',31,46),('south dormitory',32,57)]


def eight(x,y):
    return {(x+dx,y+dy) for dx in (-1,0,1) for dy in (-1,0,1) if dx or dy}


def reserved(city,z):
    cells = set(LADDERS)
    for npc in city.data['map']['npcs']:
        s=npc['schedule']
        cells.update((s[3+i],s[6+i]) for i in range(3) if s[9+i]==z)
    cells.update((c['x'],c['y']) for c in city.floor(z)['contents'])
    return cells


def furniture(city,z,items):
    """Never fill a circulation/ladder/quest cell with impassable furniture."""
    walk = movement(city)
    blocked = reserved(city,z)
    for x,y,tile in items:
        if (x,y) in blocked and walk(tile) is not True:
            raise MapError(f'Furniture conflicts with retained position: {z},{x},{y}')
        if city.floor(z)['tiles'][y][x] not in FLOORS | {0x90,0x91,0x92,0x93}:
            raise MapError(f'Furniture needs a clear indoor cell: {z},{x},{y}')
    city.paint(z,items)


def factory(city):
    old=final_footprints()['central_factory']
    for z in (0,1):
        city.paint(z,[(x,y,5) for x,y in old-FACTORY])
        city.paint(z,[(x,y,0x4f if not eight(x,y)<=FACTORY else (0x44 if z==0 else 0x40))
                      for x,y in FACTORY])
    city.paint(0,[(x,y,0xb8) for x,y in ENTRANCES])
    # Eight rooms around the hall, linked by short passages. Shared walls are
    # explicit architectural partitions, not accidental unions of room boxes.
    partitions={(29,y) for y in (18,19,20,30,31,32)}
    partitions|={(x,25) for x in (22,23,24,35,36,37)}
    partitions|={(25,y) for y in (22,23,24,26,27,28)}
    partitions|={(34,y) for y in (22,23,24,26,27,28)}
    city.paint(0,[(x,y,0x4f) for x,y in partitions if (x,y) in FACTORY])
    # Open room passages allow circulation while the external entrances remain
    # normal doors. No arch tile 0x87: it would imply scheduled gate records.
    openings={(29,20),(29,30),(24,25),(35,25),(25,24),(25,27),(34,24),(34,27)}
    city.paint(0,[(x,y,0x44) for x,y in openings])
    hall=polygon([(27,21),(32,21),(34,23),(34,27),(32,29),(27,29),(25,27),(25,23)])
    city.paint(0,[(x,y,0x4f) for x,y in hall if not eight(x,y)<=hall])
    passages={(29,21),(30,21),(29,29),(30,29),(25,24),(34,24),
              (25,27),(26,27),(34,27),(33,27)}
    city.paint(0,[(x,y,0x44) for x,y in passages])
    for x,y in LADDERS:
        if (x,y) in FACTORY:
            city.paint(0,[(x,y,0xc8)])
            city.paint(1,[(x,y,0xc9)])
    return city


def shore(city):
    g=city.floor(0)['tiles']
    city.paint(0,[(x,y,5) for y,row in enumerate(g) for x,t in enumerate(row) if t==7])
    sea,_=hydrology(True)
    bank=set().union(*(neighbors(x,y) for x,y in sea))-sea
    for x,y in bank:
        if city.floor(0)['tiles'][y][x] not in (5,6,8):
            continue
        wet=frozenset((a-x,b-y) for a,b in neighbors(x,y) if (a,b) in sea)
        tile={frozenset({(-1,0)}):0x34,frozenset({(1,0)}):0x35,
              frozenset({(0,1)}):0x36,frozenset({(0,-1)}):0x37}.get(wet)
        if tile is not None:
            city.paint(0,[(x,y,tile)])


def corner_caps(city):
    """Cap concave joints: a floor must not touch grass through a diagonal gap."""
    footprints=final_footprints();footprints['central_factory']=FACTORY
    for z in (0,1):
        protected=reserved(city,z)
        for name,cells in footprints.items():
            wall=0x4d if 'dormitory' in name else 0x4f
            changes=[]
            for x,y in cells:
                tile=city.floor(z)['tiles'][y][x]
                if (neighbors(x,y)<=cells and not eight(x,y)<=cells and
                        tile in FLOORS and (x,y) not in protected):
                    changes.append((x,y,wall))
            city.paint(z,changes)


def roads(city):
    # Re-route the former factory-side dirt tracks around the new footprint.
    for y in range(20,33):
        if city.floor(0)['tiles'][y][20] in (5,6,8,0x20,0x21,0x22,0x23,0x24,0x25,0x26):
            city.paint(0,[(20,y,0x44)])
    g=city.floor(0)['tiles']
    city.paint(0,[(x,y,0x44) for y,row in enumerate(g) for x,t in enumerate(row)
                  if 0x20<=t<=0x26])
    # Dormitory doors open onto short dirt spurs, not the paved production routes.
    city.paint(0,[(11,25,0x4d),(7,30,0xb8),(32,60,0x4d),(36,60,0xb8),
                   (36,59,0x40)])
    for points in DIRT_SPURS:
        dirt_path(city,0,points)
    return city


def baseline():
    if hashlib.sha256(SOURCE.read_bytes()).hexdigest()!=SOURCE_SHA256:
        raise MapError('Authoritative export changed; re-audit the furnishing study')
    if hashlib.sha256(BASE.read_bytes()).hexdigest()!=BASE_SHA256:
        raise MapError('Prior layout changed; re-audit the architecture revision')
    city=CityMap.load(SOURCE)
    city.protect_original()
    for z in (0,1):
        city.authorize_terrain(z,[(x,y) for y in range(32) for x in range(32)],AUTHORIZATION)
    # Prior outputs remain immutable. Copy their complete, already validated data.
    city.data=CityMap.load(BASE).data
    return city


def write_study(output):
    output.mkdir(parents=True,exist_ok=False)
    city=baseline();stages=[];shots=[]
    for name,edit in [('01-architecture',factory),('02-furnished',furnish),('03-final',finish)]:
        edit(city)
        city.save(output/f'{name}.json')
        for z,label in ((0,'ground'),(1,'upper')):
            render(city,z).save(output/f'{name}-{label}.png')
        stages.append({'stage':name,'preservationErrors':city.preservation(),
                       'ground':analyze(city,0,ARRIVAL,WORK_TARGETS),
                       'scheduleConflicts':schedule_audit(city)})
        shots.append(CityMap(city.data))
    for label,region in [('factory',(19,15,22,21)),('homes',(15,4,34,15)),
                          ('dormitories',(1,2,15,31)),('shore',(0,46,26,18))]:
        render(city,region=region,scale=3).save(output/f'closeup-{label}.png')
    compare(CityMap.load(BASE),city).save(output/'comparison.png')
    manifest={'source':'maps/Minoc.json','sourceSha256':SOURCE_SHA256,
              'baseStudy':'tools/city_design/experiments/minoc_u7/study/04-final.json',
              'baseStudySha256':hashlib.sha256(BASE.read_bytes()).hexdigest(),
              'authorizedTerrainChanges':[{'floor':z,'cells':[[x,y] for y in range(32) for x in range(32)],
                                            'reason':AUTHORIZATION} for z in (0,1)],
              'authorizedMetadataChanges':[],'arrival':{'cell':list(ARRIVAL),'requiresCompanionMod':True},
              'stages':stages,'gamePlayTested':False}
    (output/'validation.json').write_text(json.dumps(manifest,indent=2)+'\n')
    return city


def furnish(city):
    # First seal the old concave joints, before furniture could hide a floor gap.
    corner_caps(city)
    furniture(city,0,[
        # Reference factory: office/tool rooms, four workshops and two stores.
        (27,18,0xa5),(27,19,0x90),(28,18,0x5c),
        (31,18,0x5d),(33,18,0xad),(32,20,0xaf),
        (22,22,0xbc),(23,22,0x58),(24,22,0xfc),(23,23,0x90),
        (22,26,0xbc),(23,26,0x58),(24,26,0xfc),(23,27,0x90),
        (35,22,0x94),(36,22,0x96),(35,23,0x90),(36,23,0x90),(37,22,0xa6),
        (35,26,0x58),(36,26,0xfc),(37,26,0xbc),(35,27,0x90),
        (28,24,0x94),(29,24,0x95),(30,24,0x96),
        (28,23,0x92),(29,23,0x92),(30,23,0x92),(28,25,0x90),(30,25,0x90),
        (29,27,0x94),(30,27,0x95),(31,27,0x96),
        (30,26,0x92),(31,26,0x92),(30,28,0x90),(31,28,0x90),
        (33,25,0xbe),(32,23,0xae),
        (26,31,0x9a),(27,31,0x9c),(26,32,0x90),(27,32,0x90),(26,30,0xad),
        (31,30,0xa6),(32,30,0xa7),(33,31,0xaf),
        # Domestic bedrooms and dining groups, with actual native facing chairs.
        (21,7,0xad),(21,8,0x9d),(18,8,0xae),(20,9,0x92),
        (20,11,0x90),(21,11,0x90),(18,11,0x5c),
        (44,10,0xad),(44,11,0x9d),(41,11,0xae),
        (43,13,0x92),(44,13,0x92),(43,15,0x90),(44,15,0x90),(39,14,0xbf),
        (42,23,0xae),(46,23,0xae),(47,24,0xad),(44,22,0x9d),(45,24,0x5b),
        (45,25,0x92),(46,25,0x92),(47,25,0x92),
        (45,27,0x90),(46,27,0x90),(47,27,0x90),(44,27,0xad),
        # Hall and shop counters get seats, shelves, practical storage and lamps.
        (16,16,0x90),(18,15,0x5c),(17,15,0xae),
        (29,8,0x90),(30,9,0x92),(30,11,0x90),(31,11,0x90),
        (33,10,0x5c),(27,10,0x5b),
        (14,22,0x92),(16,22,0x92),(14,24,0x90),(16,24,0x90),
        (14,26,0x92),(16,26,0x92),(14,28,0x90),(16,28,0x90),(18,22,0xad),(18,24,0xaf),
        (13,37,0x92),(14,37,0x92),(13,39,0x90),(14,39,0x90),(10,38,0x5c),(11,39,0xaf),
        (24,36,0x92),(26,36,0x92),(24,38,0x90),(26,38,0x90),(29,38,0x5c),(30,38,0xad),
        (10,46,0x91),(12,45,0xad),(12,46,0x9d),(11,46,0xa5),(10,47,0xae),
        # Shipwright and river works: workbench seating and tool storage.
        (6,49,0x92),(7,49,0x92),(6,51,0x90),
        (3,52,0x92),(5,52,0x92),(3,54,0x90),(5,54,0x90),(7,55,0xad),(10,55,0xaf),
        (45,45,0x92),(47,45,0x92),(45,47,0x90),(47,47,0x90),(45,43,0x90),
        (58,55,0x92),(59,55,0x92),(56,54,0x90),
    ])
    # Sparse loft work floor becomes a staffed ledger/storage room, avoiding all
    # retained destinations and the ladder. No change to the NPC schedules.
    furniture(city,1,[(28,23,0x94),(29,23,0x96),(28,22,0x92),(29,22,0x92),
                      (28,24,0x90),(29,24,0x90),(33,19,0xad),(31,18,0x5c),
                      (35,22,0xa6),(36,22,0xaf),(35,27,0xa5),(35,28,0x90)])
    for z in (0,1):
        furniture(city,z,[(13,5,0xad),(13,7,0xad),(13,9,0x9d),(6,5,0xae),(6,9,0xae),
                           (10,20,0xad),(10,22,0xad),(10,24,0x9d),(6,20,0xae),(6,24,0xae),
                           (4,12,0x91),(5,12,0x94),(6,12,0x96),(7,12,0x93),
                           (7,29,0x91),(8,29,0xae)])
    furniture(city,0,[(28,44,0xad),(28,46,0xad),(34,44,0xae),(34,48,0xae),(28,48,0x9d),
                      (31,49,0x91),(32,49,0xae),
                      (36,55,0xad),(36,57,0xad),(29,55,0xae),(29,57,0xae),(36,58,0x9d),
                      (29,59,0x91),(30,59,0xae)])
    return city


def finish(city):
    shore(city);corner_caps(city);roads(city)
    return city


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True)
    write_study(p.parse_args().output)
