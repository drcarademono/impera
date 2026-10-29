"""Hand-authored, replayable Minoc studies. Never changes maps/Minoc.json.

Run: python -m tools.city_design.experiments.minoc.design --output NEW_DIRECTORY
"""

import argparse
import hashlib
import json
from pathlib import Path

from ...art import compare, render
from ...layout import analyze
from ...model import CityMap
from ...patterns import building, house, workshop

ROOT = Path(__file__).resolve().parents[4]
SOURCE = ROOT / 'maps/Minoc.json'
SOURCE_SHA256 = '878ab6e0d37afa44469758cbd0e75b1b22efc82f2919283b81a7623a5ae565a2'
START = (15, 31)
NEW_START = (32, 62)
WALL_AUTHORIZATION = ('User requested an irregular expanded perimeter and bottom-center gate; '
                      'only declared old exterior/gateway and new perimeter-border cells may change.')
TARGETS = [('industrial lane', 35, 32), ('foundry entrance', 47, 30),
           ('foundry work floor', 47, 25), ('housing door', 28, 53),
           ('housing common room', 28, 49), ('timber yard', 47, 54),
           ('timber work floor', 48, 48)]


def road(city, points, tile=0x44, width=2):
    changes = []
    for (ax, ay), (bx, by) in zip(points, points[1:]):
        if ax != bx and ay != by:
            raise ValueError('Specify orthogonal street segments explicitly')
        for y in range(min(ay, by), max(ay, by) + width):
            for x in range(min(ax, bx), max(ax, bx) + width):
                changes.append((x, y, tile))
    city.paint(0, changes)


def roof(city, x, y, rows):
    city.stamp(1, x, y, [[None if v is None else (0x27 if i % 2 == 0 else 0x28)
                           for i, v in enumerate(row)] for row in rows])


def initial(source):
    city = CityMap(source.data)
    city.protect_original()
    for floor in (0, 1):
        city.resize(floor, 64, 64, 5)
    # Broad grass buffer leaves old walls untouched. Managed coppice to the east,
    # irregular shelter belts along the southern edge; no random scatter.
    for x, y, w, h, tile in ((58, 9, 3, 21, 8), (60, 29, 3, 16, 6),
                             (40, 58, 17, 3, 8), (17, 59, 15, 3, 6),
                             (35, 7, 9, 3, 8), (54, 12, 6, 3, 6)):
        city.rectangle(0, (x, y, w, h), tile)
    # The original east wall has no gate. All access leaves the existing south
    # gate first, then bends around the southeast corner rather than cutting it.
    road(city, [(15, 32), (15, 38), (35, 38), (35, 17)])
    road(city, [(35, 32), (47, 32), (47, 30)], tile=0x20)
    road(city, [(27, 38), (27, 54), (47, 54), (47, 52)], tile=0x20)
    # Three buildings, deliberately different footprints and functions.
    foundry = workshop(14, 12, wing=(0, 0, 4, 3), door=(7, 11), windows=False)
    city.stamp(0, 40, 18, foundry)
    # Work bays: bellows/anvil/fireplace, with a central carrying aisle.
    city.paint(0, [(51, 20, 0xbc), (50, 20, 0x58), (49, 20, 0xfc),
                   (51, 23, 0xbc), (50, 23, 0x58), (49, 23, 0xfc),
                   (42, 24, 0xa6), (42, 25, 0xa6), (42, 26, 0xa6)])
    housing = house(11, 9, wing=(8, 0, 3, 3), door=(5, 8))
    city.stamp(0, 23, 44, housing)
    city.paint(0, [(24, 47, 0xab), (25, 47, 0xac), (24, 49, 0x94),
                   (25, 49, 0x95), (26, 49, 0x96), (30, 50, 0xa6)])
    mill = building(14, 10, door=(4, 9), wing=(10, 7, 4, 3), floor=0x40)
    city.stamp(0, 43, 43, mill)
    city.paint(0, [(45, 45, 0xa3), (46, 45, 0xa3), (47, 45, 0xa3),
                   (51, 45, 0x94), (52, 45, 0x95), (53, 45, 0x96),
                   (51, 48, 0x94), (52, 48, 0x95), (53, 48, 0x96),
                   (44, 50, 0xa6)])
    for x, y, rows in ((40, 18, foundry), (23, 44, housing), (43, 43, mill)):
        roof(city, x, y, rows)
    return city


def revision(city):
    # Visual review: the foundry doorstep opens onto a thin, disconnected spur;
    # the warehouse needs a credible cart turning yard; housing needs privacy.
    road(city, [(35, 33), (47, 33), (47, 30)], tile=0x44)
    city.rectangle(0, (40, 30, 14, 4), 0x45)
    # Keep a cobbled walking seam through the mottled stone work yard.
    road(city, [(35, 33), (47, 33), (47, 30)])
    city.rectangle(0, (43, 53, 14, 4), 0x20)
    city.paint(0, [(54, 53, 0xa3), (55, 53, 0xa3), (56, 53, 0xa3),
                   (54, 55, 0xa3), (55, 55, 0xa3), (56, 55, 0xa3)])
    # Original housing spur ran through its interior to reach the front door.
    # Route around the west side, then approach from the south instead.
    for y in range(40, 44):
        city.rectangle(0, (27, y, 2, 1), 5)
    road(city, [(20, 38), (20, 54), (28, 54), (28, 53)], tile=0x20)
    city.paint(0, [(23, 54, 0xca), (24, 54, 0xca), (25, 54, 0xca),
                   (30, 54, 0xca), (31, 54, 0xca), (32, 54, 0xca),
                   (23, 56, 0x2d), (24, 56, 0x2d), (25, 56, 0x2d)])
    return city


def final_revision(city):
    # Review of revision 1: fence at y54 occupied the east–west footpath.
    # Move it north beside the frontage and restore the entire public lane.
    city.paint(0, [(x, 54, 0x20) for x in (23, 24, 25, 30, 31, 32)] +
                   [(x, 53, 0xca) for x in (23, 24, 25, 30, 31, 32)])
    # Short, offset service approach makes the foundry's work yard purposeful.
    city.rectangle(0, (54, 30, 2, 2), 0x20)
    city.paint(0, [(54, 29, 0xa6), (55, 29, 0xa6), (58, 47, 0xa3), (58, 49, 0xa3)])
    return city


def furnished_revision(city):
    # Full-map review exposed an oversized, underfurnished foundry and a vestige
    # of the old housing spur. Give the hall two work islands and storage bays,
    # retaining the central north–south carrying aisle at x47–48.
    city.rectangle(0, (27, 39, 2, 5), 5)
    city.paint(0, [(44, 23, 0x58), (44, 26, 0x58), (45, 23, 0xfc),
                   (45, 26, 0xfc), (42, 22, 0x94), (43, 22, 0x96),
                   (51, 26, 0xa6), (52, 26, 0xa6),
                   (41, 31, 0x4c), (42, 31, 0x4c), (41, 32, 0x4c),
                   (53, 33, 0xbd), (21, 43, 0xbd), (42, 54, 0xbd)])
    # Two screened sleeping bays open onto the shared eastern aisle; keep the
    # stove and communal table south of the screens, not in a sleeping bay.
    city.paint(0, [(x, y, 0x4f) for y in (46, 48) for x in (24, 25, 26)] +
                   [(30, 47, 0xad)])
    # Timber cutout becomes a covered loading recess instead of unused grass.
    city.rectangle(0, (53, 50, 3, 2), 0x40)
    city.paint(0, [(54, 50, 0xa3), (55, 50, 0xa3)])
    return city


def paved_revision(city):
    # User review correctly rejected the striping: 0x20 is a narrow N–S path
    # component, not seamless dirt. Replace every experimental use with the same
    # continuous brick paving as the old town. Never touch the protected town.
    f = city.floor(0)
    city.paint(0, [(x, y, 0x44) for y, row in enumerate(f['tiles'])
                   for x, tile in enumerate(row) if (x >= 32 or y >= 32) and tile == 0x20])
    return city


def authorized_wall_cells(source, floor):
    """Exact old exterior/gateway masonry, excluding buildings and sign text."""
    f = source.floor(floor)
    old = [(x, y) for y, row in enumerate(f['tiles']) for x, tile in enumerate(row)
            if (tile == 0x4f and ((x == 30 and 2 <= y <= 30) or
                                (y == 30 and 2 <= x <= 29) or
                                (28 <= y <= 29 and 11 <= x <= 19))) or
               (x == 15 and y == 28 and tile == 0xba)]
    return sorted(set(old) | {(31, 1), (1, 31)})


def perimeter_points():
    # A grown settlement, not a scaled-up square: northern setbacks, eastern
    # industrial bulge, western meadow inset and projecting south gate apron.
    return [(1, 1), (34, 1), (34, 5), (45, 5), (45, 10), (59, 10),
            (59, 35), (61, 35), (61, 54), (58, 54), (58, 60), (39, 60),
            (39, 61), (25, 61), (25, 59), (8, 59), (8, 48), (3, 48),
            (3, 35), (1, 35), (1, 1)]


def perimeter_revision(city):
    source = CityMap(city.original)
    for z in (0, 1):
        cells = authorized_wall_cells(source, z)
        city.authorize_terrain(z, cells, WALL_AUTHORIZATION)
        city.paint(z, [(x, y, 0x44 if (x, y) == (15, 28) else 5) for x, y in cells])
        wall = []
        for (ax, ay), (bx, by) in zip(perimeter_points(), perimeter_points()[1:]):
            wall += [(x, y, 0x4f) for y in range(min(ay, by), max(ay, by) + 1)
                     for x in range(min(ax, bx), max(ax, bx) + 1)]
        city.paint(z, wall)
        # Open arch at the wall, passage immediately below, centered on x31.5.
        if z == 0:
            city.paint(z, [(31, 61, 0x87), (32, 61, 0x87),
                           (31, 62, 0x44), (32, 62, 0x44), (31, 63, 0x44), (32, 63, 0x44)])
    # Bend east before the lodging: a straight gate-to-spine road would cut
    # through its eastern rooms. The service spine at x35 clears all footprints.
    road(city, [(31, 61), (31, 54), (35, 54), (35, 38)], tile=0x44)
    # Restore the arch after paving its north-side approach.
    city.paint(0, [(31, 61, 0x87), (32, 61, 0x87)])
    # Minoc is native location ID 5 (engine_table_names.h, one-based). Always-open
    # gates avoid an inferred night-time barrier on the player's arrival cell.
    gates = [[5, 0, x, 62, 1, 2, 0x44, 0x99, 5, 20, 0, 0, 0, 0, 0, 0] for x in (31, 32)]
    city.authorize_metadata(('map', 'floors', 0, 'gates'), gates,
                            'User requested the relocated entrance gate; add two always-open gate cells.')
    city.floor(0)['gates'] = gates
    return city


def write_study(output):
    if hashlib.sha256(SOURCE.read_bytes()).hexdigest() != SOURCE_SHA256:
        raise ValueError('Authoritative Minoc changed; visually re-audit and update the recipe first')
    source = CityMap.load(SOURCE)
    if source.data['map']['name'] != 'Minoc' or any(
            (f['width'], f['height']) != (32, 32) for f in source.data['map']['floors']):
        raise ValueError('Re-audit the study before applying it to changed Minoc dimensions')
    output.mkdir(parents=True, exist_ok=False)
    render(source).save(output / '00-original-ground.png')
    render(source, 1).save(output / '00-original-upper.png')
    stages, reports = [], []
    city = initial(source)
    for name, change in (('01-initial', None), ('02-revised', revision),
                         ('03-access-revised', final_revision), ('04-furnished', furnished_revision),
                         ('05-paved', paved_revision), ('06-final', perimeter_revision)):
        if change:
            change(city)
        city.validate()
        city.save(output / f'{name}.json')
        render(city).save(output / f'{name}-ground.png')
        render(city, 1).save(output / f'{name}-upper.png')
        reports.append({'stage': name, 'preservationErrors': city.preservation(),
                        'ground': analyze(city, 0, NEW_START if name == '06-final' else START, TARGETS),
                        'upper': analyze(city, 1)})
        stages.append(CityMap(city.data))
    compare(stages[0], stages[-1]).save(output / 'comparison-initial-final.png')
    render(city, region=(38, 16, 20, 20), scale=3).save(output / 'closeup-foundry.png')
    render(city, region=(18, 41, 18, 19), scale=3).save(output / 'closeup-housing.png')
    render(city, region=(40, 40, 21, 19), scale=3).save(output / 'closeup-timber.png')
    render(city, region=(26, 55, 14, 9), scale=4).save(output / 'closeup-gate.png')
    manifest = {'source': 'maps/Minoc.json', 'sourceSha256': hashlib.sha256(SOURCE.read_bytes()).hexdigest(),
                'sourceCommit': '1074c9c1c067698d53883412d5cbbecd9dd29011',
                'stages': reports,
                'authorizedTerrainChanges': [{'floor': z, 'cells': authorized_wall_cells(source, z),
                                               'reason': WALL_AUTHORIZATION} for z in (0, 1)],
                'authorizedMetadataChanges': [{'path': list(path), 'value': value, 'reason': reason}
                                              for path, (value, reason) in city.authorized_metadata.items()],
                'arrival': {'cell': list(NEW_START), 'requiresCompanionMod': True,
                            'reason': 'Location JSON has no portal-arrival field; use the native mod.'},
                'workshopImportStatus': 'See native-validation.json; Python generation does not run Workshop.',
                'gamePlayTested': False}
    (output / 'validation.json').write_text(json.dumps(manifest, indent=2) + '\n')
    return manifest


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    write_study(args.output)
