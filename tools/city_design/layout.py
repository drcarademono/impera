"""Conservative, explicit-target accessibility analysis; never a play-test claim."""

from collections import deque

from .art import unbase64
from .catalogue import load_catalogue

LIMITATIONS = [
    'Static four-neighbor party-on-foot analysis, not an engine simulation or play-test.',
    'Actors, time-dependent gates/drawbridges, locks, scripts, diagonals, vehicles and '
    'dynamic objects can alter accessibility; inspect these in Workshop and play-test.',
    'Door-relaxed routes are conditional, not verified traversable routes.',
    'Terrain rendering omits NPC overlays, runtime animation, lighting and sprite-ground inference.',
    'Custom type records receive structural checks, not a complete TYPES_Validate proof; '
    'use the native Workshop importer probe for authoritative validation.',
    'Reachable does not mean safe: poison, fire and other terrain effects still need review.',
]


def movement(city):
    base = {r['id']: r for r in load_catalogue()['tiles']}
    overrides = {d['id']: unbase64(d['record']) for group in ('vanillaTypes', 'customTypes')
                 for d in city.data[group]}

    def state(tile):
        if tile in overrides:
            d = overrides[tile]
            # TYPES_DefWalkable for party shape 0x1c, profile zero (seats allowed).
            return bool(int.from_bytes(d[12:14], 'little') & 1) if d[8] == 0 else None
        return base[tile]['partyWalkable'] if tile in base else None
    return state


def reachable(city, floor, start, doors=False):
    f, state = city.floor(floor), movement(city)
    city.region(floor, (*start, 1, 1))
    def allowed(x, y):
        tile = f['tiles'][y][x]
        return state(tile) is True or (doors and tile in (0x97, 0x98, 0xb8, 0xb9, 0xba, 0xbb))
    if not allowed(*start):
        return set()
    seen, queue = {tuple(start)}, deque([tuple(start)])
    while queue:
        x, y = queue.popleft()
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            p = x + dx, y + dy
            if (0 <= p[0] < f['width'] and 0 <= p[1] < f['height'] and p not in seen
                    and allowed(*p)):
                seen.add(p)
                queue.append(p)
    return seen


def analyze(city, floor=0, start=None, targets=()):
    """Targets are named entrances/interior work cells selected by the designer."""
    f, state = city.floor(floor), movement(city)
    report = {'floor': floor, 'errors': [], 'warnings': [], 'targets': [],
              'limitations': list(LIMITATIONS)}
    report['hazards'] = [{'x': x, 'y': y, 'tile': tile}
                         for y, row in enumerate(f['tiles']) for x, tile in enumerate(row)
                         if tile in (4, 0x8f, 0xbc)]
    unknown = sorted({tile for row in f['tiles'] for tile in row if state(tile) is None})
    if unknown:
        report['warnings'].append({'unknownMovementTiles': unknown})
    if city.data['map']['kind'] == 'dungeon':
        report['errors'].append('Dungeon feature movement is unsupported')
        return report
    if start is not None:
        strict, relaxed = reachable(city, floor, start), reachable(city, floor, start, doors=True)
        if not strict:
            report['errors'].append('Start is blocked or has unknown movement')
        report['reachableCells'] = len(strict)
        report['walkableCellsOutsideStartComponent'] = sum(
            state(tile) is True and (x, y) not in strict
            for y, row in enumerate(f['tiles']) for x, tile in enumerate(row))
        for name, x, y in targets:
            city.region(floor, (x, y, 1, 1))
            status = 'reachable' if (x, y) in strict else 'door-dependent' if (x, y) in relaxed else 'unreachable'
            report['targets'].append({'name': name, 'x': x, 'y': y, 'status': status})
            if status == 'unreachable':
                report['errors'].append(f'{name}: inaccessible from start')
            elif status == 'door-dependent':
                report['warnings'].append(f'{name}: route requires door/lock interaction')
    # Door openings need a usable approach on opposite sides, not just one neighbor.
    for y, row in enumerate(f['tiles']):
        for x, tile in enumerate(row):
            if tile not in (0xb8, 0xb9, 0xba, 0xbb, 0x87, 0x97, 0x98):
                continue
            pairs = (((x - 1, y), (x + 1, y)), ((x, y - 1), (x, y + 1)))
            if not any(all(0 <= px < f['width'] and 0 <= py < f['height'] and
                           state(f['tiles'][py][px]) is True for px, py in pair) for pair in pairs):
                report['warnings'].append(f'Entrance at {x},{y} lacks opposite walkable approaches')
    floors = {f['floor']: f for f in city.data['map']['floors']}
    for y, row in enumerate(f['tiles']):
        for x, tile in enumerate(row):
            if tile not in range(0xc4, 0xca):
                continue
            if tile in (0xc8, 0xc9):
                z, expected = floor + (1 if tile == 0xc8 else -1), (0xc9 if tile == 0xc8 else 0xc8)
                dest = floors.get(z)
                if not dest or x >= dest['width'] or y >= dest['height']:
                    report['warnings'].append(f'Ladder {x},{y} has no destination floor/cell {z}')
                elif dest['tiles'][y][x] != expected:
                    report['warnings'].append(f'Ladder {x},{y} lacks reciprocal ladder on floor {z}')
            else:
                # TOWN_052e depends on approach direction; do not assume +/- from artwork.
                report['warnings'].append(f'Directional stair {x},{y} needs approach-direction '
                                          'and adjacent-floor validation in Workshop/game')
    if any(gate[5] == 1 for gate in f.get('gates', [])):
        report['warnings'].append('Scheduled gates exist; static route checks cannot certify them')
    return report
