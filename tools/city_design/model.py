"""Lossless map editing: only dimensions, terrain and used-art masks may change."""

import copy
import hashlib
import json
from collections import Counter
from pathlib import Path

from .art import decode_tiles, unbase64

MAX_JSON = 128 * 1024 * 1024


class MapError(ValueError):
    pass


def integer(value, low, high, field):
    if type(value) is not int or not low <= value <= high:
        raise MapError(f'{field}: expected integer {low}..{high}')
    return value


def array(value, field):
    if not isinstance(value, list):
        raise MapError(f'{field}: expected array')
    return value


def obj(value, field):
    if not isinstance(value, dict):
        raise MapError(f'{field}: expected object')
    return value


def record(value, length, field):
    if len(array(value, field)) != length:
        raise MapError(f'{field}: expected {length} bytes')
    for byte in value:
        integer(byte, 0, 255, field)


def point(value, floor, field):
    if len(array(value, field)) != 2:
        raise MapError(f'{field}: expected [x,y]')
    integer(value[0], 0, floor['width'] - 1, field + '.x')
    integer(value[1], 0, floor['height'] - 1, field + '.y')


def _unique_pairs(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise MapError(f'Duplicate JSON key: {key}')
        result[key] = value
    return result


def _metadata(data):
    """Include unknown fields in the preservation contract, not a hand-picked subset."""
    result = copy.deepcopy(data)
    def strip(node):
        for f in node['floors']:
            for key in ('width', 'height', 'tiles'):
                f.pop(key, None)
        # Linked destinations and encounter rooms are not edited by this API;
        # their complete terrain and metadata remain protected as opaque data.
    strip(result['map'])
    result['artwork'].pop('slots', None)
    return result


class CityMap:
    def __init__(self, data, source=None):
        self.data = copy.deepcopy(data)
        self.source = Path(source).resolve() if source else None
        self.validate()
        self.original = copy.deepcopy(self.data)
        self.protected = []
        self.authorized_terrain = {}
        self.authorized_metadata = {}

    @classmethod
    def load(cls, path):
        path = Path(path)
        if path.stat().st_size > MAX_JSON:
            raise MapError('Map JSON exceeds 128 MiB')
        try:
            data = json.loads(path.read_text(encoding='utf-8'), object_pairs_hook=_unique_pairs,
                              parse_constant=lambda v: (_ for _ in ()).throw(MapError(v)))
        except (json.JSONDecodeError, UnicodeError, RecursionError) as exc:
            raise MapError(f'Invalid JSON: {exc}') from exc
        return cls(data, path)

    def floor(self, index):
        for floor in self.data['map']['floors']:
            if floor['floor'] == index:
                return floor
        raise MapError(f'No floor {index}')

    def region(self, floor, rectangle):
        f = self.floor(floor)
        if not isinstance(rectangle, (list, tuple)) or len(rectangle) != 4:
            raise MapError('Region must be [x,y,width,height]')
        x, y, w, h = rectangle
        integer(x, 0, f['width'] - 1, 'region.x')
        integer(y, 0, f['height'] - 1, 'region.y')
        integer(w, 1, f['width'] - x, 'region.width')
        integer(h, 1, f['height'] - y, 'region.height')
        return x, y, w, h

    def validate(self):
        root = obj(self.data, 'root')
        if root.get('format') != 'impera-map' or type(root.get('version')) is not int or root['version'] != 1:
            raise MapError('Expected impera-map version 1')
        definitions = {}
        for group, low, high in (('customTypes', 512, 65535), ('vanillaTypes', 0, 511)):
            for entry in array(root.get(group), group):
                obj(entry, group)
                tile = integer(entry.get('id'), low, high, group + '.id')
                if tile in definitions:
                    raise MapError('Duplicate type definition')
                raw = unbase64(entry.get('record'))
                if not 256 <= len(raw) < 16 * 1024 * 1024 or int.from_bytes(raw[:4], 'little') != len(raw):
                    raise MapError('Invalid type record size')
                if int.from_bytes(raw[4:8], 'little') != tile:
                    raise MapError('Type ID does not match record')
                if raw[8] > 2 or int.from_bytes(raw[12:14], 'little') > 2047:
                    raise MapError('Invalid type kind or movement mask')
                definitions[tile] = raw
        art = obj(root.get('artwork'), 'artwork')
        decode_tiles(unbase64(art.get('dos')))
        slots = array(art.get('slots'), 'artwork.slots')
        for slot in slots:
            integer(slot, 0, 511, 'artwork slot')
        if len(set(slots)) != len(slots):
            raise MapError('Duplicate artwork slot')
        for sheet in array(art.get('sheets'), 'artwork.sheets'):
            obj(sheet, 'sheet')
            if not isinstance(sheet.get('resource'), str) or not unbase64(sheet.get('png')):
                raise MapError('Invalid alternate sheet')
        self._validate_node(root.get('map'), definitions)
        return self

    def _validate_node(self, node, definitions, depth=0):
        obj(node, 'map')
        if depth > 3:
            raise MapError('Nested map depth exceeds Workshop limit')
        kind = node.get('kind')
        if kind not in ('location', 'world', 'combat', 'dungeon') or not isinstance(node.get('name'), str):
            raise MapError('Invalid map kind or name')
        floors = array(node.get('floors'), 'floors')
        if not floors or (kind == 'location' and len(floors) > 5):
            raise MapError('Invalid floor count')
        by_floor = {}
        for f in floors:
            obj(f, 'floor')
            z = integer(f.get('floor'), -1, 7, 'floor index')
            if z in by_floor or (kind == 'location' and z > 3):
                raise MapError('Duplicate/invalid floor index')
            by_floor[z] = f
            w, h = (integer(f.get(key), 1, 256, key) for key in ('width', 'height'))
            integer(f.get('background'), 0, 255, 'background')
            rows = array(f.get('tiles'), 'tiles')
            if len(rows) != h:
                raise MapError('Wrong tile row count')
            for row in rows:
                if len(array(row, 'tile row')) != w:
                    raise MapError('Wrong tile column count')
                for tile in row:
                    integer(tile, 0, 255 if kind == 'dungeon' else 65535, 'tile')
                    if tile >= 512 and tile not in definitions:
                        raise MapError(f'Missing custom type {tile}')
                    if kind == 'dungeon' and tile >> 4 == 9:
                        raise MapError('Unused dungeon feature')
            for sign in array(f.get('signs'), 'signs'):
                obj(sign, 'sign')
                point([sign.get('x'), sign.get('y')], f, 'sign')
                if not isinstance(sign.get('text'), str):
                    raise MapError('Invalid sign text')
            for content in array(f.get('contents'), 'contents'):
                obj(content, 'contents entry')
                point([content.get('x'), content.get('y')], f, 'contents')
                for key, lo, hi in (('action', 0, 1), ('refill', 0, 4), ('original', 0, 65535),
                                    ('slot', 1, 255), ('flags', 0, 3),
                                    ('lockDifficulty', 0, 30), ('trapDifficulty', 0, 30)):
                    integer(content.get(key), lo, hi, key)
                if content['refill'] == 2 and content['original'] not in (13, 14, 15):
                    raise MapError('Invalid conditional discovery')
                items = array(content.get('items'), 'items')
                if len(items) > 16:
                    raise MapError('Too many container items')
                for item in items:
                    obj(item, 'item')
                    for key, lo, hi in (('type', 1, 9), ('variant', 0, 47), ('quantity', 1, 9999)):
                        integer(item.get(key), lo, hi, key)
            for gate in array(f.get('gates', []), 'gates'):
                record(gate, 16, 'gate')
                point(gate[2:4], f, 'gate')
            if 'partyStart' in f:
                point(f['partyStart'], f, 'partyStart')
            if kind == 'combat':
                record(f.get('encounterData'), 352, 'encounterData')
                for actor in array(f.get('actors'), 'actors'):
                    obj(actor, 'actor')
                    integer(actor.get('slot'), 0, 15, 'actor slot')
                    if actor.get('type') not in definitions:
                        raise MapError('Missing encounter custom actor type')
                if 'markers' in f and (len(array(f['markers'], 'markers')) != 64 or
                                       any(type(x) is not bool for x in f['markers'])):
                    raise MapError('Invalid combat marker mask')
        if kind == 'location':
            if 0 not in by_floor:
                raise MapError('Location needs a ground floor')
            for key, value in obj(node.get('dialogues'), 'dialogues').items():
                if not key.isdigit() or not 0 <= int(key) <= 255 or not unbase64(value):
                    raise MapError('Invalid conversation')
            seen = set()
            for npc in array(node.get('npcs'), 'npcs'):
                obj(npc, 'NPC')
                slot = integer(npc.get('slot'), 1, 32767, 'NPC slot')
                if slot in seen:
                    raise MapError('Duplicate NPC slot')
                seen.add(slot)
                integer(npc.get('appearance'), 1, 255, 'NPC appearance')
                integer(npc.get('conversation'), 0, 255, 'NPC conversation')
                record(npc.get('schedule'), 16, 'schedule')
                if 'type' in npc and npc['type'] not in definitions:
                    raise MapError('Missing NPC custom type')
                s = npc['schedule']
                for i in range(3):
                    z = s[9 + i] if s[9 + i] < 128 else s[9 + i] - 256
                    if z not in by_floor:
                        raise MapError(f'NPC {slot} schedule refers to missing floor {z}')
                    point([s[3 + i], s[6 + i]], by_floor[z], 'NPC schedule')
        if kind == 'combat' and node.get('encounterMode') not in ('outdoor', 'dungeon'):
            raise MapError('Missing encounter mode')
        if kind == 'world':
            integer(node.get('world'), 0, 1, 'world')
            objects = array(node.get('objects'), 'objects')
            if len(objects) != 31 or {o.get('slot') for o in objects if isinstance(o, dict)} != set(range(1, 32)):
                raise MapError('World needs 31 unique object slots')
            for o in objects:
                record(o.get('data'), 8, 'world object')
            for moon in array(node.get('moonstones'), 'moonstones'):
                obj(moon, 'moonstone')
                integer(moon.get('phase'), 0, 7, 'moon phase')
                point([moon.get('x'), moon.get('y')], floors[0], 'moonstone')
            for link in array(node.get('entrances'), 'entrances'):
                obj(link, 'entrance')
                point([link.get('x'), link.get('y')], floors[0], 'entrance')
                integer(link.get('id'), 1, 255, 'entrance id')
                if 'destination' in link:
                    self._validate_node(link['destination'], definitions, depth + 1)
                    target = next(f for f in link['destination']['floors'] if f['floor'] == 0)
                    point(link.get('arrival'), target, 'arrival')
                else:
                    record(link.get('arrival'), 2, 'arrival')
        if kind == 'dungeon':
            rooms = array(node.get('rooms'), 'rooms')
            if len(rooms) != 16:
                raise MapError('Dungeon needs sixteen encounter rooms')
            for room in rooms:
                obj(room, 'room')
                if room.get('kind') != 'combat' or room.get('encounterMode') != 'dungeon':
                    raise MapError('Invalid dungeon room')
                self._validate_node(room, definitions, depth + 1)

    def protect(self, floor, rectangle):
        x, y, w, h = self.region(floor, rectangle)
        original = next(f for f in self.original['map']['floors'] if f['floor'] == floor)
        current = self.floor(floor)
        snapshot = [[original['tiles'][cy][cx] if cy < original['height'] and cx < original['width']
                     else current['tiles'][cy][cx] for cx in range(x, x + w)]
                    for cy in range(y, y + h)]
        self.protected.append((floor, (x, y, w, h), snapshot))

    def protect_original(self):
        for f in self.original['map']['floors']:
            self.protect(f['floor'], (0, 0, f['width'], f['height']))

    def authorize_terrain(self, floor, cells, reason):
        """Narrow, reviewable exception after explicit authorization, never implicit."""
        if not reason.strip():
            raise MapError('Terrain exception requires an authorization reason')
        for x, y in cells:
            self.region(floor, (x, y, 1, 1))
            self.authorized_terrain[(floor, x, y)] = reason

    def authorize_metadata(self, path, value, reason):
        """Permit one exact metadata replacement, not arbitrary metadata mutation."""
        if not path or not reason.strip():
            raise MapError('Metadata exception requires a path and authorization reason')
        self.authorized_metadata[tuple(path)] = (copy.deepcopy(value), reason)

    def paint(self, floor, changes):
        """Atomic edit: validate all cells, protections and assets before mutation."""
        f = self.floor(floor)
        changes = list(changes)
        known = {d['id'] for d in self.data['customTypes']}
        for x, y, tile in changes:
            self.region(floor, (x, y, 1, 1))
            integer(tile, 0, 255 if self.data['map']['kind'] == 'dungeon' else 65535, 'tile')
            if self.data['map']['kind'] == 'dungeon' and tile >> 4 == 9:
                raise MapError('Unused dungeon feature')
            if tile >= 512 and tile not in known:
                raise MapError(f'Missing custom type {tile}')
            for z, (px, py, pw, ph), _ in self.protected:
                if (z == floor and px <= x < px + pw and py <= y < py + ph and
                        f['tiles'][y][x] != tile and (floor, x, y) not in self.authorized_terrain):
                    raise MapError(f'Protected cell ({floor},{x},{y})')
        for x, y, tile in changes:
            f['tiles'][y][x] = tile
        # The importer only copies these slots. New vanilla terrain must join the mask.
        if self.data['map']['kind'] != 'dungeon':
            self.data['artwork']['slots'] = sorted(set(self.data['artwork']['slots']) |
                                                  {tile for _, _, tile in changes if tile < 512})

    def rectangle(self, floor, rectangle, tile):
        x, y, w, h = self.region(floor, rectangle)
        self.paint(floor, ((cx, cy, tile) for cy in range(y, y + h) for cx in range(x, x + w)))

    def stamp(self, floor, x, y, rows):
        """None cells are transparent; irregular footprints are welcome."""
        if not rows or not rows[0] or any(len(row) != len(rows[0]) for row in rows):
            raise MapError('Pattern must be rectangular and nonempty')
        self.region(floor, (x, y, len(rows[0]), len(rows)))
        self.paint(floor, ((x + dx, y + dy, tile) for dy, row in enumerate(rows)
                           for dx, tile in enumerate(row) if tile is not None))

    def resize(self, floor, width, height, fill):
        f = self.floor(floor)
        integer(width, 1, 256, 'width')
        integer(height, 1, 256, 'height')
        integer(fill, 0, 255, 'fill')
        for z, (x, y, w, h), _ in self.protected:
            if z == floor and (x + w > width or y + h > height):
                raise MapError('Resize would crop a protected region')
        previous = copy.deepcopy(self.data)
        old = f['tiles']
        f['tiles'] = [[old[y][x] if y < f['height'] and x < f['width'] else fill
                       for x in range(width)] for y in range(height)]
        f['width'], f['height'] = width, height
        if self.data['map']['kind'] != 'dungeon':
            self.data['artwork']['slots'] = sorted(set(self.data['artwork']['slots']) | {fill})
        try:
            self.validate()
        except ValueError:
            self.data = previous
            raise

    def preservation(self):
        errors = []
        expected_metadata = _metadata(self.original)
        for path, (value, _) in self.authorized_metadata.items():
            parent = expected_metadata
            for key in path[:-1]:
                parent = parent[key]
            parent[path[-1]] = copy.deepcopy(value)
        if _metadata(self.data) != expected_metadata:
            errors.append('Metadata/artwork changed (including unknown fields)')
        for z, (x, y, w, h), snapshot in self.protected:
            after = self.floor(z)
            for cy in range(y, y + h):
                for cx in range(x, x + w):
                    if ((z, cx, cy) not in self.authorized_terrain and
                            (cy >= after['height'] or cx >= after['width'] or
                             snapshot[cy-y][cx-x] != after['tiles'][cy][cx])):
                        errors.append(f'Protected terrain changed: floor {z}, {cx},{cy}')
        old_slots = set(self.original['artwork']['slots'])
        if not old_slots <= set(self.data['artwork']['slots']):
            errors.append('Original artwork slots removed')
        return errors

    def summary(self):
        return {'name': self.data['map']['name'], 'kind': self.data['map']['kind'],
                'sourceSha256': hashlib.sha256(json.dumps(self.original, sort_keys=True).encode()).hexdigest(),
                'npcs': len(self.data['map'].get('npcs', [])),
                'floors': [{'floor': f['floor'], 'width': f['width'], 'height': f['height'],
                            'tiles': dict(sorted(Counter(t for row in f['tiles'] for t in row).items())),
                            'signs': len(f['signs']), 'contents': len(f['contents'])}
                           for f in self.data['map']['floors']]}

    def save(self, path):
        """Exclusive creation rejects overwrite, symlinks and the source path."""
        path = Path(path)
        if self.source and path.resolve() == self.source:
            raise MapError('Experimental output cannot overwrite its source')
        self.validate()
        errors = self.preservation()
        if errors:
            raise MapError('; '.join(errors))
        text = json.dumps(self.data, ensure_ascii=False, indent=2, allow_nan=False) + '\n'
        if len(text.encode()) > MAX_JSON:
            raise MapError('Map JSON exceeds 128 MiB')
        with path.open('x', encoding='utf-8') as output:
            output.write(text)
