"""Direction-aware native path components; never repeat a north–south path as fill."""

from .model import MapError

# Verified against the embedded DOS artwork at the four tile edges. These are
# narrow path components with grass around them, not seamless dirt fill tiles.
ROAD_CONNECTIONS = {
    0x20: frozenset('NS'), 0x21: frozenset('EW'), 0x22: frozenset('NE'),
    0x23: frozenset('ES'), 0x24: frozenset('SW'), 0x25: frozenset('NW'),
    0x26: frozenset('NESW'),
}


def dirt_path(city, floor, points):
    """One-cell-wide orthogonal polyline, matched to its neighboring components.

    Adjacent existing path tiles join the network. Native artwork has no T tile:
    T nodes use the explicit four-way junction, with a visible spare arm. For a
    broad industrial road use continuous brick/stone paving, not parallel paths.
    """
    cells = set()
    if len(points) < 2:
        raise MapError('A path needs at least two points')
    for (ax, ay), (bx, by) in zip(points, points[1:]):
        if ax != bx and ay != by:
            raise MapError('Path segments must be orthogonal')
        for x in range(min(ax, bx), max(ax, bx) + 1):
            for y in range(min(ay, by), max(ay, by) + 1):
                city.region(floor, (x, y, 1, 1))
                cells.add((x, y))
    f = city.floor(floor)
    existing = {(x, y) for y, row in enumerate(f['tiles']) for x, tile in enumerate(row)
                if tile in ROAD_CONNECTIONS}
    affected = set(cells)
    directions = ((0, -1, 'N'), (1, 0, 'E'), (0, 1, 'S'), (-1, 0, 'W'))
    for x, y in cells:
        affected.update((x + dx, y + dy) for dx, dy, _ in directions if (x + dx, y + dy) in existing)
    network = cells | existing
    changes = []
    for x, y in affected:
        neighbors = frozenset(d for dx, dy, d in directions if (x + dx, y + dy) in network)
        if len(neighbors) >= 3:
            tile = 0x26
        elif neighbors in (frozenset('N'), frozenset('S'), frozenset('NS')):
            tile = 0x20
        elif neighbors in (frozenset('E'), frozenset('W'), frozenset('EW')):
            tile = 0x21
        else:
            tile = next((t for t, connection in ROAD_CONNECTIONS.items() if connection == neighbors), None)
        if tile is None:
            raise MapError('Isolated path cell')
        changes.append((x, y, tile))
    city.paint(floor, changes)
