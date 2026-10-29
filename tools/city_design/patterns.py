"""Composable architectural vocabulary, not random city generation."""

from .model import MapError


def building(width, height, *, door=None, wing=None, floor=0x44, windows=True):
    """A room or L-footprint assembled from a main rectangle plus an explicit wing.

    wing=(x,y,w,h) is a rectangle removed from a corner, leaving transparent cells.
    door=(x,y) must replace a boundary wall; furnishing remains a deliberate edit.
    """
    if type(width) is not int or type(height) is not int or width < 5 or height < 5:
        raise MapError('Buildings need at least a 5x5 footprint')
    cells = {(x, y) for y in range(height) for x in range(width)}
    if wing:
        x, y, w, h = wing
        if min(x, y) < 0 or min(w, h) < 1 or x + w > width or y + h > height:
            raise MapError('Invalid corner cutout')
        if not (x in (0, width - w) and y in (0, height - h)):
            raise MapError('Cutout must meet a corner')
        cells -= {(cx, cy) for cy in range(y, y + h) for cx in range(x, x + w)}
    rows = [[None] * width for _ in range(height)]
    for x, y in cells:
        boundary = any((x + dx, y + dy) not in cells for dx, dy in ((0, 1), (0, -1), (1, 0), (-1, 0)))
        rows[y][x] = 0x4f if boundary else floor
    dx, dy = door or (width // 2, height - 1)
    if not (0 <= dx < width and 0 <= dy < height) or rows[dy][dx] != 0x4f:
        raise MapError('Door must occupy a boundary wall')
    rows[dy][dx] = 0xb8
    if windows:
        for x in range(2, width - 2, 3):
            if rows[0][x] == 0x4f and (x, 1) in cells and rows[1][x] != 0x4f:
                rows[0][x] = 0x4b
    return rows


def house(width=7, height=7, **kwargs):
    rows = building(width, height, floor=0x40, **kwargs)
    for x, y, tile in ((1, 1, 0xab), (2, 1, 0xac), (width - 2, 1, 0xad),
                       (1, height - 2, 0xbf)):
        if rows[y][x] in (0x40, 0x44):
            rows[y][x] = tile
    return rows


def workshop(width=11, height=9, **kwargs):
    rows = building(width, height, **kwargs)
    for x, y, tile in ((1, 1, 0xfc), (2, 1, 0x58), (width - 2, 1, 0xbc),
                       (1, height - 2, 0xa6), (width - 2, height - 2, 0xa3)):
        if rows[y][x] in (0x40, 0x44):
            rows[y][x] = tile
    return rows


def shop(width=8, height=7, **kwargs):
    rows = building(width, height, **kwargs)
    rows[2][1:4] = [0x94, 0x95, 0x96]
    rows[1][width - 2] = 0xa6
    return rows


def inn(width=12, height=9, **kwargs):
    rows = building(width, height, **kwargs)
    for x in range(1, width - 3, 3):
        if rows[1][x] in (0x40, 0x44) and rows[1][x + 1] in (0x40, 0x44):
            rows[1][x:x + 2] = [0xab, 0xac]
    rows[height - 3][1:4] = [0x94, 0x95, 0x96]
    return rows
