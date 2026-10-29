"""A fresh 64x64 Minoc, hand-planned from the user's Ultima VII reference."""

import argparse
import hashlib
import json
from pathlib import Path

from PIL import Image, ImageDraw

from ...art import compare, render
from ...layout import analyze, movement
from ...model import CityMap, MapError
from ...paths import dirt_path
from ..minoc.design import SOURCE, SOURCE_SHA256

SIZE = 64
ARRIVAL = (32, 62)
AUTHORIZATION = 'User requested a fresh 64x64 Minoc terrain based on the Ultima VII reference, with no town wall.'
FLOORS = {0x40, 0x44, 0x45}

# Rectangles are intentionally different shapes and scales; overlapping rooms
# form coherent footprints, not a random building-placement algorithm.
BUILDINGS = {
    'northwest_house': [(18, 6, 6, 7)],
    'northern_hall': [(28, 6, 4, 10), (25, 9, 10, 5)],
    'northeast_house': [(40, 9, 6, 9), (38, 12, 3, 4)],
    'west_small_shop': [(15, 14, 5, 5)],
    'western_workshops': [(13, 21, 7, 12)],
    'central_factory': [(26, 21, 8, 9), (23, 18, 5, 6), (28, 17, 5, 5),
                        (33, 20, 5, 6), (22, 26, 6, 5), (33, 27, 5, 5), (26, 30, 8, 4)],
    'eastern_residence': [(40, 21, 9, 5), (43, 25, 6, 4)],
    'southwest_store': [(9, 35, 7, 7)],
    'southern_workshop': [(20, 35, 12, 6)],
    'harbor_house': [(9, 44, 5, 5)],
    'shipbuilder': [(2, 48, 7, 9), (8, 53, 4, 4)],
    'river_factory': [(43, 41, 7, 8)],
    'east_bank_factory': [(55, 52, 8, 6), (60, 57, 3, 4)],
    'north_dormitory': [(25, 43, 13, 8)],
    'south_dormitory': [(26, 54, 12, 7)],
}
EXTRA_DORMS = {'northwest_loft_dormitory': (3, 4, 12, 10),
               'western_loft_dormitory': (3, 19, 9, 12)}
LADDERS = [(13, 12), (10, 28), (32, 26)]
DOORS = {
    'northwest_house': [(20, 12)], 'northern_hall': [(30, 15)],
    'northeast_house': [(42, 17)], 'west_small_shop': [(17, 18)],
    'western_workshops': [(19, 26), (16, 32)],
    'central_factory': [(26, 18), (22, 28), (37, 23), (30, 33)],
    'eastern_residence': [(40, 23)], 'southwest_store': [(12, 41)],
    'southern_workshop': [(25, 40)], 'harbor_house': [(11, 48)],
    'shipbuilder': [(11, 54), (5, 56)], 'river_factory': [(43, 45), (46, 48)],
    'east_bank_factory': [(55, 55), (58, 52)],
    'north_dormitory': [(31, 50)], 'south_dormitory': [(32, 60)],
}

# A continuous northern pond/headrace feeds an eastern south-flowing channel.
# The short western arm terminates in a spring pool, like the reference.
RIVER_LINES = [([(43, 40), (50, 40), (51, 41), (51, 45), (53, 46),
                 (54, 49), (53, 53), (53, 57), (51, 61), (51, 63)], 2),
               ([(43, 40), (42, 42), (42, 47), (41, 50), (40, 54), (39, 56)], 1)]
BRIDGES = {(42, 45), (41, 50), (53, 55), (54, 55)}
PATHS = [
    [(27, 1), (27, 4), (24, 4), (24, 16)],
    [(20, 13), (24, 13)], [(30, 16), (30, 16)],
    [(20, 16), (20, 20), (21, 20), (21, 32), (20, 32), (20, 34),
     (39, 34), (39, 16), (20, 16)],
    [(17, 19), (20, 19)], [(20, 26), (21, 26)], [(16, 33), (16, 34), (20, 34)],
    [(26, 16), (26, 17)], [(21, 28), (21, 28)], [(38, 23), (39, 23)],
    [(30, 34), (30, 34)], [(42, 18), (42, 19), (39, 19)],
    [(6, 34), (20, 34)], [(12, 42), (19, 42), (19, 34)],
    [(25, 41), (25, 42), (19, 42)],
    [(11, 49), (11, 50), (12, 50), (12, 54)],
    [(12, 54), (14, 54), (14, 46), (19, 46), (19, 42)],
    [(39, 34), (42, 34), (42, 38), (40, 38), (40, 45), (41, 45)],
    [(39, 34), (39, 38), (38, 38), (38, 42), (39, 42), (39, 50), (40, 50)],
    [(42, 50), (44, 50), (44, 52), (51, 52), (51, 55), (52, 55)],
    [(42, 34), (47, 34), (47, 36), (51, 36), (51, 38), (54, 38),
     (54, 40), (56, 40), (56, 43), (58, 43), (58, 46), (61, 46),
     (61, 49), (58, 49), (58, 51)],
    [(32, 63), (32, 62), (25, 62), (25, 52), (24, 52), (24, 42), (19, 42)],
    [(25, 52), (31, 52), (31, 51)], [(32, 61), (32, 62)],
]
TARGETS = [(name, *xy) for name, xy in (
    ('central factory', (29, 26)), ('river factory', (46, 45)),
    ('east-bank factory', (58, 55)), ('shipbuilder', (6, 53)),
    ('north dormitory aisle', (31, 46)), ('south dormitory aisle', (32, 57)),
    ('north street', (27, 2)), ('shipyard pier', (5, 59)))]


def rect_cells(rect):
    x, y, w, h = rect
    return {(cx, cy) for cy in range(y, y + h) for cx in range(x, x + w)}


def footprint(name):
    return set().union(*(rect_cells(r) for r in BUILDINGS[name]))


def final_footprints():
    result = {name: footprint(name) for name in BUILDINGS}
    result['northwest_house'] = {(x-1, y) for x, y in result['northwest_house']}
    result['central_factory'] |= rect_cells((23, 21, 5, 6))
    result.update({name: rect_cells(rect) for name, rect in EXTRA_DORMS.items()})
    return result


def polygon(points):
    mask = Image.new('1', (SIZE, SIZE))
    ImageDraw.Draw(mask).polygon(points, fill=1)
    return {(x, y) for y in range(SIZE) for x in range(SIZE) if mask.getpixel((x, y))}


def line(points, width=1):
    mask = Image.new('1', (SIZE, SIZE))
    ImageDraw.Draw(mask).line(points, fill=1, width=width)
    return {(x, y) for y in range(SIZE) for x in range(SIZE) if mask.getpixel((x, y))}


def neighbors(x, y):
    return {(x+dx, y+dy) for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))
            if 0 <= x+dx < SIZE and 0 <= y+dy < SIZE}


def sea_cells(revised=False):
    points = ([(0, 49), (1, 49), (1, 54), (4, 58), (8, 60), (13, 61),
               (18, 59), (21, 57), (22, 54), (23, 53), (24, 54),
               (24, 59), (24, 63), (0, 63)] if revised else
              [(0, 48), (3, 48), (3, 51), (6, 54), (10, 56), (16, 57),
               (20, 55), (21, 52), (23, 52), (23, 56), (24, 59), (24, 63), (0, 63)])
    return polygon(points) - footprint('shipbuilder')


def hydrology(revised=False):
    sea = sea_cells(revised)
    river_lines = RIVER_LINES if not revised else [RIVER_LINES[0],
        ([(43, 40), (42, 40), (42, 47), (41, 47), (41, 51),
          (40, 51), (40, 54), (39, 54), (39, 56)], 1)]
    river = set().union(*(line(points, width) for points, width in river_lines))
    river |= polygon([(42, 40), (44, 39), (49, 39), (51, 40), (50, 41), (43, 41)])
    river |= {(39, 56), (39, 57), (40, 56)}
    # Quayside foundations deliberately project into the bay; they are not sea.
    sea -= footprint('shipbuilder')
    if revised:
        river -= set().union(*(footprint(name) for name in BUILDINGS))
    return sea, river


def paint_water(city):
    sea, river = hydrology()
    water = sea | river
    changes = []
    for x, y in sea:
        changes.append((x, y, 3 if any(p not in sea for p in neighbors(x, y)) else 2))
    changes += [(x, y, 2) for x, y in river]
    beaches = set().union(*(neighbors(x, y) for x, y in sea)) - water
    changes += [(x, y, 7) for x, y in beaches]
    # Wider channels use native directional bank tiles at their land margins.
    banks = set().union(*(neighbors(x, y) for x, y in river)) - water
    for x, y in banks:
        wet = {(px-x, py-y) for px, py in neighbors(x, y) if (px, py) in river}
        tile = {frozenset({(-1, 0)}): 0x34, frozenset({(1, 0)}): 0x35,
                frozenset({(0, 1)}): 0x36, frozenset({(0, -1)}): 0x37}.get(frozenset(wet))
        if tile is not None:
            changes.append((x, y, tile))
    city.paint(0, changes)
    return sea, river


def coastal_revision(city):
    """Visual review: leave a real sandy peninsula under the shipyard, not a
    building cut straight into open sea. Keep the river separate from the bay.
    """
    old_sea, river = hydrology()
    new_sea = sea_cells(True)
    occupied = set().union(*(footprint(name) for name in BUILDINGS))
    pier = rect_cells((4, 57, 3, 4)) | rect_cells((6, 59, 5, 2))
    region = rect_cells((0, 47, 25, 17)) - occupied - pier
    # Replace the previous coastline only; retain roads above the shore.
    for x, y in region:
        if city.floor(0)['tiles'][y][x] in (2, 3, 7):
            city.paint(0, [(x, y, 5)])
    city.paint(0, [(x, y, 3 if any(p not in new_sea for p in neighbors(x, y)) else 2)
                   for x, y in new_sea - pier])
    beach = set().union(*(neighbors(x, y) for x, y in new_sea)) - new_sea - river
    beach |= set().union(*(neighbors(x, y) for x, y in beach)) - new_sea - river
    city.paint(0, [(x, y, 7) for x, y in beach - occupied - pier
                   if city.floor(0)['tiles'][y][x] in (5, 6, 8, 7)])
    # Review identified the anvil blocking the eastern factory door approach.
    city.paint(0, [(36, 23, 0x44), (35, 24, 0x58)])
    # Third dormitory bed row, keeping the central carrying aisle free.
    city.paint(0, [(x+dx, 48, tile) for x in (26, 29, 32, 35)
                   for dx, tile in ((0, 0xab), (1, 0xac))])
    # The pier should have a public quay approach, not require crossing the shop.
    dirt_path(city, 0, [(12, 54), (12, 57), (10, 57), (10, 58)])
    return city


def river_revision(city):
    _, river = hydrology(True)
    occupied = set().union(*(footprint(name) for name in BUILDINGS))
    area = rect_cells((38, 37, 18, 27)) - occupied - BRIDGES
    # Clear old channel/bank pixels, retaining roads, factory floors and props.
    city.paint(0, [(x, y, 5) for x, y in area
                   if city.floor(0)['tiles'][y][x] in (2, 3, 0x34, 0x35, 0x36, 0x37)])
    if river & occupied:
        raise MapError('River enters a factory footprint')
    city.paint(0, [(x, y, 2) for x, y in river - BRIDGES])
    banks = set().union(*(neighbors(x, y) for x, y in river)) - river - occupied - BRIDGES
    for x, y in banks:
        wet = frozenset((px-x, py-y) for px, py in neighbors(x, y) if (px, py) in river)
        tile = {frozenset({(-1, 0)}): 0x34, frozenset({(1, 0)}): 0x35,
                frozenset({(0, 1)}): 0x36, frozenset({(0, -1)}): 0x37}.get(wet)
        if tile is not None and city.floor(0)['tiles'][y][x] in (5, 6, 8, 9):
            city.paint(0, [(x, y, tile)])
    city.paint(0, [(x, y, 0x6a) for x, y in BRIDGES])
    return city


def shell(city, z, cells, doors, wall, floor):
    city.paint(z, [(x, y, 0xb8 if (x, y) in doors else
                   wall if not neighbors(x, y) <= cells else floor) for x, y in cells])


def integration_revision(city):
    # The user permits fresh terrain, but not silent changes to named NPCs or
    # quest records. Furnished lofts in empty western plots fit their old positions.
    for name, (x, y, w, h) in EXTRA_DORMS.items():
        cells = rect_cells((x, y, w, h))
        doors = {(9, 13)} if name.startswith('northwest') else {(11, 25)}
        for z in (0, 1):
            shell(city, z, cells, doors if z == 0 else set(), 0x4d, 0x40)
            beds = [(bx+dx, by, tile) for by in range(y+1, y+h-1, 2)
                    for bx in range(x+1, x+w-2, 3)
                    for dx, tile in ((0, 0xab), (1, 0xac))]
            city.paint(z, beds)
    # Move the small northern cottage one cell west: its old corner occupied an
    # unchanged NPC destination. The relative reference layout is unaffected.
    cells = footprint('northwest_house')
    for z in (0, 1):
        tiles = [(x-1, y, city.floor(z)['tiles'][y][x]) for x, y in cells]
        city.paint(z, [(x, y, 5) for x, y in cells])
        city.paint(z, tiles)
    # A doorless service opening and moved bookshelf clear two old destinations
    # in the northern hall, without creating an inferred time-controlled gate.
    city.paint(0, [(26, 11, 0x44), (26, 13, 0x44), (25, 11, 0x44), (33, 12, 0x5c)])
    old = footprint('central_factory')
    props = [(x, y, city.floor(0)['tiles'][y][x]) for x, y in old
             if city.floor(0)['tiles'][y][x] not in (0x44, 0x4f, 0xb8)]
    cells = final_footprints()['central_factory']
    shell(city, 0, cells, set(DOORS['central_factory']), 0x4f, 0x44)
    city.paint(0, props)
    shell(city, 1, cells, set(), 0x4f, 0x40)
    city.paint(1, [(29, 22, 0x94), (30, 22, 0x95), (31, 22, 0x96),
                   (35, 22, 0xa6), (35, 23, 0xa6), (24, 20, 0xa5),
                   (26, 19, 0x5c), (32, 31, 0xa6)])
    # Preserve the original containers, now with readable cupboard/trunk artwork.
    city.paint(1, [(c['x'], c['y'], 0xaf) for c in city.floor(1)['contents']])
    for x, y in LADDERS:
        city.paint(0, [(x, y, 0xc8)])
        city.paint(1, [(x, y, 0xc9)])
    dirt_path(city, 0, [(9, 14), (12, 14), (12, 34)])
    dirt_path(city, 0, [(19, 13), (20, 13)])
    dirt_path(city, 0, [(26, 14), (24, 14)])
    return city


def water_components(cells):
    left, sizes = set(cells), []
    while left:
        queue, size = [left.pop()], 0
        while queue:
            size += 1
            new = neighbors(*queue.pop()) & left
            left -= new
            queue.extend(new)
        sizes.append(size)
    return sorted(sizes, reverse=True)


def construct(city, name):
    cells = footprint(name)
    doors = set(DOORS[name])
    if not doors <= cells:
        raise MapError(f'{name}: door outside footprint')
    rough = name.endswith('dormitory')
    wall, floor = (0x4d, 0x40) if rough else (0x4f, 0x44)
    if name in ('shipbuilder', 'harbor_house', 'northeast_house'):
        floor = 0x40
    changes = []
    for x, y in cells:
        boundary = not neighbors(x, y) <= cells
        tile = 0xb8 if (x, y) in doors else wall if boundary else floor
        changes.append((x, y, tile))
    city.paint(0, changes)
    # The existing upper-floor metadata remains, while this new architecture
    # receives roofs rather than invented upper-floor rooms/schedules.
    city.paint(1, [(x, y, 0x27 if x % 2 else 0x28) for x, y in cells])


def furnish(city):
    props = [
        (19, 7, 0xab), (20, 7, 0xac), (21, 10, 0x94), (22, 10, 0x96),
        (29, 7, 0xa5), (30, 10, 0x94), (31, 10, 0x96), (26, 11, 0x5c), (33, 11, 0x5d),
        (41, 10, 0xab), (42, 10, 0xac), (43, 14, 0x94), (44, 14, 0x96),
        (16, 15, 0xa5), (18, 16, 0xa6),
        (14, 23, 0x94), (15, 23, 0x95), (16, 23, 0x96),
        (14, 27, 0x94), (15, 27, 0x95), (16, 27, 0x96), (18, 30, 0xa6),
        (24, 19, 0xbc), (25, 20, 0x58), (29, 18, 0xfc), (31, 19, 0x58),
        (35, 21, 0xbc), (36, 23, 0x58), (24, 28, 0x58), (25, 28, 0xfc),
        (28, 23, 0x94), (29, 23, 0x95), (30, 23, 0x96),
        (28, 28, 0x94), (29, 28, 0x95), (30, 28, 0x96),
        (35, 29, 0x58), (36, 29, 0xfc), (28, 31, 0xa6), (32, 31, 0xa6),
        (41, 22, 0xab), (42, 22, 0xac), (46, 22, 0xab), (47, 22, 0xac),
        (45, 26, 0x94), (46, 26, 0x95), (47, 26, 0x96),
        (10, 36, 0xa6), (11, 36, 0xa6), (13, 38, 0x94), (14, 38, 0x96),
        (21, 36, 0xa3), (22, 36, 0xa3), (24, 37, 0x94), (25, 37, 0x95), (26, 37, 0x96),
        (29, 36, 0xa6), (30, 36, 0xa6), (10, 45, 0xab), (11, 45, 0xac), (12, 47, 0xbf),
        (3, 49, 0xa3), (4, 49, 0xa3), (6, 50, 0x94), (7, 50, 0x96),
        (3, 53, 0x94), (4, 53, 0x95), (5, 53, 0x96), (9, 55, 0xa6),
        (44, 42, 0xfc), (45, 42, 0x58), (48, 42, 0xbc),
        (45, 46, 0x94), (46, 46, 0x95), (47, 46, 0x96), (48, 47, 0xa6),
        (56, 53, 0x58), (57, 53, 0xfc), (61, 53, 0xbc),
        (58, 56, 0x94), (59, 56, 0x96), (61, 59, 0xa6),
    ]
    for x, y, tile in props:
        if city.floor(0)['tiles'][y][x] not in FLOORS:
            raise MapError(f'Furniture would replace a wall/outdoor cell at {x},{y}')
    city.paint(0, props)
    for name in ('north_dormitory', 'south_dormitory'):
        x, y, w, h = BUILDINGS[name][0]
        beds = [(bx+dx, by, tile) for by in (y+1, y+3) for bx in range(x+1, x+w-2, 3)
                for dx, tile in ((0, 0xab), (1, 0xac))]
        city.paint(0, beds)
        city.paint(0, [(x+1, y+h-2, 0xbf), (x+w-2, y+h-2, 0xa6)])


def streets(city):
    footprints = set().union(*(footprint(name) for name in BUILDINGS))
    sea, river = hydrology()
    for points in sorted(PATHS, key=lambda p: len(set(p)) == 1):
        cells = set().union(*(line([a, b]) for a, b in zip(points, points[1:])))
        bad = cells & (footprints | sea | river)
        if bad:
            raise MapError(f'Street crosses a building or water: {sorted(bad)}')
        dirt_path(city, 0, points)
    city.paint(0, [(x, y, 0x6a) for x, y in BRIDGES])
    # Explicit wharf: timber belongs outdoors here, not in ordinary front yards.
    pier = rect_cells((4, 57, 3, 4)) | rect_cells((6, 59, 5, 2))
    city.paint(0, [(x, y, 0x40) for x, y in pier])
    city.paint(0, [(4, 60, 0xa2), (10, 60, 0xa2)])
    return pier


def landscape(city):
    occupied = set().union(*(footprint(name) for name in BUILDINGS))
    for points in ([(2, 8), (8, 7), (11, 11), (9, 15), (3, 14)],
                   [(2, 20), (7, 18), (10, 22), (7, 28), (2, 27)],
                   [(51, 13), (57, 12), (61, 18), (58, 26), (53, 23)],
                   [(6, 29), (10, 28), (12, 31), (8, 32)],
                   [(55, 28), (60, 27), (62, 33), (58, 36)],
                   [(15, 50), (18, 48), (21, 49), (19, 53)],
                   [(43, 58), (46, 56), (48, 60), (47, 63)]):
        for x, y in polygon(points) - occupied:
            if city.floor(0)['tiles'][y][x] == 5:
                city.paint(0, [(x, y, 8 if (x+2*y) % 4 else 6)])
    # Individual trees frame places, not a random scatter of obstacles.
    for x, y in ((12, 7), (17, 9), (36, 8), (47, 15), (10, 24),
                 (18, 31), (34, 37), (18, 39), (22, 46), (44, 55), (58, 32)):
        if city.floor(0)['tiles'][y][x] == 5:
            city.paint(0, [(x, y, 9)])


def initial():
    if hashlib.sha256(SOURCE.read_bytes()).hexdigest() != SOURCE_SHA256:
        raise MapError('Authoritative export changed; re-audit the fresh study')
    city = CityMap.load(SOURCE)
    city.protect_original()
    for z in (0, 1):
        city.authorize_terrain(z, [(x, y) for y in range(32) for x in range(32)], AUTHORIZATION)
        city.resize(z, SIZE, SIZE, 5)
        city.rectangle(z, (0, 0, SIZE, SIZE), 5)
    paint_water(city)
    for name in BUILDINGS:
        construct(city, name)
    furnish(city)
    streets(city)
    landscape(city)
    return city


def schedule_audit(city):
    state = movement(city)
    problems = []
    for npc in city.data['map']['npcs']:
        s = npc['schedule']
        for i in range(3):
            x, y, z = s[3+i], s[6+i], s[9+i]
            z = z if z < 128 else z-256
            tile = city.floor(z)['tiles'][y][x]
            if state(tile) is not True:
                problems.append({'npcSlot': npc['slot'], 'scheduleIndex': i,
                                 'cell': [x, y, z], 'tile': tile})
    return problems


def write_study(output):
    output.mkdir(parents=True, exist_ok=False)
    city = initial()
    stages = []
    snapshots = []
    for name, revision in (('01-layout', None), ('02-coast-revised', coastal_revision),
                            ('03-river-revised', river_revision), ('04-final', integration_revision)):
        if revision:
            revision(city)
        city.save(output/f'{name}.json')
        for z, label in ((0, 'ground'), (1, 'upper')):
            render(city, z).save(output/f'{name}-{label}.png')
        stages.append({'stage': name, 'preservationErrors': city.preservation(),
                       'ground': analyze(city, 0, ARRIVAL, TARGETS),
                       'legacyScheduleConflicts': schedule_audit(city)})
        snapshots.append(CityMap(city.data))
    manifest = {'source': 'maps/Minoc.json', 'sourceSha256': SOURCE_SHA256,
                'authorizedTerrainChanges': [{'floor': z, 'cells': [[x, y] for y in range(32) for x in range(32)],
                                              'reason': AUTHORIZATION} for z in (0, 1)],
                'authorizedMetadataChanges': [], 'arrival': {'cell': list(ARRIVAL), 'requiresCompanionMod': True},
                'riverCells': [list(p) for p in sorted(hydrology(True)[1])],
                'riverWaterComponents': water_components(hydrology(True)[1]),
                'stages': stages,
                'gamePlayTested': False}
    compare(snapshots[0], snapshots[-1]).save(output/'comparison.png')
    for name, area in (('waterfront', (0, 46, 26, 18)), ('river', (38, 37, 26, 27)),
                        ('central-factory', (20, 15, 20, 20)), ('dormitories', (23, 41, 17, 22))):
        render(city, region=area, scale=3).save(output/f'closeup-{name}.png')
    render(city, region=(1, 2, 15, 31), scale=3).save(output/'closeup-loft-dormitories.png')
    (output/'validation.json').write_text(json.dumps(manifest, indent=2)+'\n')
    return city, manifest


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    write_study(args.output)
