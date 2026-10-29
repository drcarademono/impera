"""Workshop-compatible DOS resource decoding, without distributing game artwork."""

import base64
import binascii
import struct

from PIL import Image, ImageDraw

PALETTE = (
    (0, 0, 0), (0, 0, 170), (0, 170, 0), (0, 170, 170),
    (170, 0, 0), (170, 0, 170), (170, 85, 0), (170, 170, 170),
    (85, 85, 85), (85, 85, 255), (85, 255, 85), (85, 255, 255),
    (255, 85, 85), (255, 85, 255), (255, 255, 85), (255, 255, 255),
)


def unbase64(value):
    if not isinstance(value, str):
        raise ValueError("Artwork/record must be a Base64 string")
    try:
        return base64.b64decode(value, validate=True)
    except (ValueError, binascii.Error) as exc:
        raise ValueError("Invalid Base64 data") from exc


def decompress(data, expected=65536):
    """Port of U5::decompress: LE size, LSB-first 9–12-bit LZW."""
    if len(data) < 4 or struct.unpack_from('<I', data)[0] != expected:
        raise ValueError(f"Expected a {expected}-byte DOS resource")
    table = [bytes([i]) for i in range(256)] + [b'', b'']
    width, next_code, bit = 9, 258, 32
    previous = b''
    output = bytearray()
    while len(output) < expected:
        if bit + width > len(data) * 8:
            raise ValueError("Truncated DOS LZW stream")
        code = sum(((data[(bit + i) // 8] >> ((bit + i) % 8)) & 1) << i
                   for i in range(width))
        bit += width
        if code == 256:
            table = [bytes([i]) for i in range(256)] + [b'', b'']
            width, next_code, previous = 9, 258, b''
            continue
        if code == 257:
            raise ValueError("DOS LZW ended before its declared size")
        if code < len(table) and table[code]:
            entry = table[code]
        elif code == next_code and previous:
            entry = previous + previous[:1]
        else:
            raise ValueError("Invalid DOS LZW dictionary code")
        output.extend(entry[:expected - len(output)])
        if previous and next_code < 4096:
            table.append(previous + entry[:1])
            next_code += 1
            if next_code == 1 << width and width < 12:
                width += 1
        previous = entry
    return bytes(output)


def decode_tiles(data):
    raw = data if len(data) == 65536 else decompress(data)
    images = []
    for tile in range(512):
        pixels = []
        for byte in raw[tile * 128:(tile + 1) * 128]:
            pixels.extend((PALETTE[byte >> 4], PALETTE[byte & 15]))
        image = Image.new('RGB', (16, 16))
        image.putdata(pixels)
        images.append(image)
    return images


class DosArt:
    def __init__(self, document, external=None):
        # Embedded artwork is authoritative; an explicit file is only a fallback.
        embedded = document['artwork']['dos']
        self.images = decode_tiles(unbase64(embedded) if embedded else external or b'')
        self.overrides = {entry['id']: unbase64(entry['record'])
                          for group in ('customTypes', 'vanillaTypes')
                          for entry in document.get(group, [])}

    def tile(self, tile_id):
        record = self.overrides.get(tile_id)
        if tile_id >= 512 or (record and int.from_bytes(record[248:250], 'little')):
            raise ValueError(f"Custom artwork for tile {tile_id} is not supported by the DOS "
                             "renderer; use Workshop. No substitute has been rendered.")
        if not 0 <= tile_id < 512:
            raise ValueError(f"Missing DOS slot {tile_id}")
        return self.images[tile_id]


def render(city, floor=0, region=None, scale=1):
    """Static terrain view matching Workshop DOS slots; no NPC/state overlays."""
    f = city.floor(floor)
    if city.data['map']['kind'] == 'dungeon':
        raise ValueError("Dungeon feature bytes are not DOS tile IDs; use Workshop")
    if type(scale) is not int or not 1 <= scale <= 8:
        raise ValueError("Nearest-neighbor scale must be an integer from 1 to 8")
    x, y, w, h = city.region(floor, region or (0, 0, f['width'], f['height']))
    if w * h * scale * scale > 262144:
        raise ValueError("Preview exceeds the 67-megapixel limit; select a smaller region")
    art = DosArt(city.data)
    image = Image.new('RGB', (w * 16, h * 16))
    for row in range(h):
        for col in range(w):
            image.paste(art.tile(f['tiles'][y + row][x + col]), (col * 16, row * 16))
    return image.resize((w * 16 * scale, h * 16 * scale), Image.Resampling.NEAREST)


def compare(before, after, floor=0, scale=1):
    """Side-by-side complete terrain, with changed cells outlined on the right."""
    a, b = render(before, floor, scale=scale), render(after, floor, scale=scale)
    image = Image.new('RGB', (a.width + b.width + 16, max(a.height, b.height)), '#202020')
    image.paste(a, (0, 0))
    image.paste(b, (a.width + 16, 0))
    draw = ImageDraw.Draw(image)
    old, new = before.floor(floor), after.floor(floor)
    edge = 16 * scale
    for y, row in enumerate(new['tiles']):
        for x, value in enumerate(row):
            if y >= old['height'] or x >= old['width'] or old['tiles'][y][x] != value:
                px, py = a.width + 16 + x * edge, y * edge
                draw.rectangle((px, py, px + edge - 1, py + edge - 1), outline='#ff9700')
    return image
