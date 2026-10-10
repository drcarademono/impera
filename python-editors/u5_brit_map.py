#!/usr/bin/env python3
"""
Ultima V – BRIT.DAT extractor & viewer
--------------------------------------

Reconstructs the full 256×256 Britannia overworld tile map from BRIT.DAT using
chunk indices from DATA.OVL (offset 0x3886). Outputs:
- A quick‑look PNG (default 4096×4096 via 16× upscale)
- An optional true‑tiles render (4096×4096) using tiles.16 (compressed or raw)

Notes
-----
• World = 16×16 chunks; each chunk = 16×16 tiles ⇒ 256×256 tiles total.
• DATA.OVL[0x3886:0x3886+256] is the chunk map: 0xFF = all‑water (tile 0x01);
  otherwise it is the index into the *compact* chunk stream in BRIT.DAT.
• BRIT.DAT stores only non‑all‑water chunks, 256 bytes each (16×16 × 1B).
• tiles.16 (uncompressed) is always 512 * 16 * 8 = **65536 bytes**.

License: MIT
"""
from __future__ import annotations
import argparse
import struct
from pathlib import Path
from typing import List, Tuple, Optional

from PIL import Image  # pillow

# --- LZW: GIF-like (works for Ultima V TILES.16) -------------------------- #

class BitStream:
    def __init__(self, data: bytes, msb_first: bool):
        self.data = data
        self.msb_first = msb_first
        self.bitpos = 0

    def read(self, nbits: int) -> int:
        acc = 0
        if self.msb_first:  # MSB->LSB in each byte (not used for tiles.16)
            for _ in range(nbits):
                if self.bitpos // 8 >= len(self.data):
                    raise EOFError
                byte = self.data[self.bitpos // 8]
                shift = 7 - (self.bitpos % 8)
                acc = (acc << 1) | ((byte >> shift) & 1)
                self.bitpos += 1
        else:               # LSB->MSB in each byte (GIF-style, needed here)
            for i in range(nbits):
                if self.bitpos // 8 >= len(self.data):
                    raise EOFError
                byte = self.data[self.bitpos // 8]
                acc |= ((byte >> (self.bitpos % 8)) & 1) << i
                self.bitpos += 1
        return acc


def lzw_decompress_tiles16_giflsb(comp: bytes, expected_len: int) -> bytes:
    """
    GIF-like LZW (no sub-blocks), min_code_size=8 => CLEAR=256, END=257,
    LSB-first packing, early-change; stops on END or EOF once expected_len is reached.
    """
    min_code_size = 8
    clear = 1 << min_code_size      # 256
    end   = clear + 1               # 257
    code_size = min_code_size + 1   # 9 bits to start
    next_code = end + 1             # 258
    max_bits = 12

    # dictionary: literal 0..255, placeholders for clear/end
    dict_seq = [bytes([i]) for i in range(clear)] + [b"", b""]
    bs = BitStream(comp, msb_first=False)
    out = bytearray()
    prev = None

    def reset():
        nonlocal code_size, next_code, dict_seq, prev
        code_size = min_code_size + 1
        next_code = end + 1
        dict_seq = [bytes([i]) for i in range(clear)] + [b"", b""]
        prev = None

    try:
        while True:
            code = bs.read(code_size)
            if code == clear:
                reset()
                continue
            if code == end:
                break

            if code < len(dict_seq) and dict_seq[code] != b"":
                entry = dict_seq[code]
            elif prev is not None and code == next_code:
                # KwKwK case
                entry = prev + prev[:1]
            else:
                # invalid code – treat as EOF
                break

            out.extend(entry)
            if expected_len and len(out) >= expected_len:
                # We've decompressed enough; OK to stop even if stream continues to END
                out = out[:expected_len]
                break

            if prev is not None and next_code < (1 << max_bits):
                dict_seq.append(prev + entry[:1])
                next_code += 1
                # early-change: bump size when we *reach* the next power-of-two
                if next_code == (1 << code_size) and code_size < max_bits:
                    code_size += 1

            prev = entry
    except EOFError:
        pass

    if len(out) != expected_len:
        raise ValueError(f"LZW decode produced {len(out)} bytes; expected {expected_len}")
    return bytes(out)

def lzw_decompress_auto(comp: bytes, expected_len: int) -> bytes:
    """Try multiple LZW variants (packing + clear/end + bump policy)."""
    variants = [
        # Ultima V tiles.16 (most likely):
        # MSB-first, CLEAR only (no END), late-change, 9→12 bits, dict freezes at 4096
        dict(name="U5: MSB, CLEAR, no END, late-change",
             init_bits=9, max_bits=12, msb_first=True, use_clear=True, use_end=False, early_change=False),

        # Classic UNIX 'compress' block mode (many encoders): MSB, CLEAR, no END, early-change
        dict(name="compress: MSB, CLEAR, no END, early-change",
             init_bits=9, max_bits=12, msb_first=True, use_clear=True, use_end=False, early_change=True),

        # UNIX w/ END (rare)
        dict(name="MSB, CLEAR+END, late-change",
             init_bits=9, max_bits=12, msb_first=True, use_clear=True, use_end=True, early_change=False),

        # GIF-style (LSB, CLEAR+END, early-change)
        dict(name="GIF: LSB, CLEAR+END, early-change",
             init_bits=9, max_bits=12, msb_first=False, use_clear=True, use_end=True, early_change=True),

        # No clear/end, MSB, early-change
        dict(name="MSB, no CLEAR/END, early-change",
             init_bits=9, max_bits=12, msb_first=True, use_clear=False, use_end=False, early_change=True),

        # No clear/end, MSB, late-change
        dict(name="MSB, no CLEAR/END, late-change",
             init_bits=9, max_bits=12, msb_first=True, use_clear=False, use_end=False, early_change=False),

        # Fixed 12-bit, MSB, no clear/end
        dict(name="MSB, fixed12, no CLEAR/END",
             init_bits=12, max_bits=12, msb_first=True, use_clear=False, use_end=False, early_change=False),
    ]

    last_len = None
    last_name = None

    for v in variants:
        name = v.pop("name")
        out = _lzw_decompress_generic(comp, expected_len=expected_len, **v)
        if out is not None:
            return out
        # Probe length (no expected_len check) so we can see what this variant yields
        out_probe = _lzw_decompress_generic(comp, expected_len=None, **v)
        if out_probe is not None:
            print(f"[lzw] variant '{name}' produced {len(out_probe)} bytes (wanted {expected_len})")
            last_len = len(out_probe)
            last_name = name

    if last_len is not None:
        raise ValueError(f"LZW auto-decode failed: closest variant '{last_name}' produced {last_len} bytes, not {expected_len}")
    raise ValueError("LZW auto-decode failed: no variant produced output")

def read_lzw_blob_auto(path: Path, fallback_expected_len: Optional[int] = None) -> bytes:
    raw = path.read_bytes()

    # If it's already the exact decompressed size, just return it.
    if fallback_expected_len is not None and len(raw) == fallback_expected_len:
        return raw

    # Try 4-byte length header (LE then BE)
    if len(raw) >= 4:
        for fmt in ('<I', '>I'):
            expected_len = struct.unpack(fmt, raw[:4])[0]
            comp = raw[4:]
            if 0 < expected_len <= 8_388_608:
                # First, try the GIF-like LZW used by TILES.16
                try:
                    return lzw_decompress_tiles16_giflsb(comp, expected_len)
                except Exception:
                    pass
                # (Optional) fallbacks: other LZW variants if you want
                # try: return lzw_decompress_auto(comp, expected_len)
                # except Exception: pass

    # Headerless attempt (rare for tiles.16, but keep as a fallback)
    if fallback_expected_len is not None:
        return lzw_decompress_tiles16_giflsb(raw, fallback_expected_len)

    raise ValueError("Could not decode LZW blob (headered and headerless attempts failed)")

# --------------------------- Constants & helpers --------------------------- #
CHUNK_SIDE = 16         # tiles
WORLD_CHUNKS = 16       # 16×16 chunks
WORLD_SIDE = CHUNK_SIDE * WORLD_CHUNKS  # 256 tiles per side
WATER_TILE_ID = 0x01

# EGA 16‑color palette as RGB tuples (PC version) – maps nibble 0..15
EGA_PALETTE: List[Tuple[int, int, int]] = [
    (0, 0, 0), (0, 0, 170), (0, 170, 0), (0, 170, 170),
    (170, 0, 0), (170, 0, 170), (170, 85, 0), (170, 170, 170),
    (85, 85, 85), (85, 85, 255), (85, 255, 85), (85, 255, 255),
    (255, 85, 85), (255, 85, 255), (255, 255, 85), (255, 255, 255),
]

# --------------------------- File readers --------------------------------- #

def read_chunk_index_table_from_data_ovl(path: Path) -> List[int]:
    """Read the 256‑byte chunk table at offset 0x3886 from DATA.OVL."""
    with path.open('rb') as f:
        f.seek(0x3886)
        data = f.read(256)
        if len(data) != 256:
            raise ValueError("DATA.OVL too short when reading chunk table (need 256 bytes)")
        return list(data)


def read_brit_chunks(path: Path) -> List[bytes]:
    """Read compact BRIT.DAT stream of non‑water chunks (N × 256 bytes)."""
    data = path.read_bytes()
    if len(data) % 256 != 0:
        raise ValueError("BRIT.DAT size is not a multiple of 256 bytes (16×16)")
    return [data[i:i+256] for i in range(0, len(data), 256)]


def reconstruct_world(chunk_map: List[int], compact_chunks: List[bytes]) -> Image.Image:
    """Build a 256×256 tile‑id image (mode 'L', values are raw tile IDs)."""
    img = Image.new('L', (WORLD_SIDE, WORLD_SIDE))
    px = img.load()

    decoded = [list(c) for c in compact_chunks]  # 256 uint8 per chunk

    for cy in range(WORLD_CHUNKS):
        for cx in range(WORLD_CHUNKS):
            i = cy * WORLD_CHUNKS + cx  # 0..255, west→east, north→south
            idx = chunk_map[i]
            base_x = cx * CHUNK_SIDE
            base_y = cy * CHUNK_SIDE
            if idx == 0xFF:
                # Fill with water
                for ty in range(CHUNK_SIDE):
                    for tx in range(CHUNK_SIDE):
                        px[base_x + tx, base_y + ty] = WATER_TILE_ID
            else:
                if idx >= len(decoded):
                    raise IndexError(f"DATA.OVL chunk index {idx} out of range (have {len(decoded)} chunks)")
                k = 0
                chunk = decoded[idx]
                for ty in range(CHUNK_SIDE):
                    for tx in range(CHUNK_SIDE):
                        px[base_x + tx, base_y + ty] = chunk[k]
                        k += 1
    return img


# --------------------------- Quick‑look coloring --------------------------- #

def colorize_quicklook(tile_img_L: Image.Image) -> Image.Image:
    """Map tile IDs to readable pseudo‑colors for fast sanity checks."""
    w, h = tile_img_L.size
    out = Image.new('RGB', (w, h))
    src = tile_img_L.load()
    dst = out.load()

    for y in range(h):
        for x in range(w):
            t = src[x, y]
            if t == WATER_TILE_ID:
                dst[x, y] = (40, 90, 200)
            elif t in (0x00, 0x10, 0x11, 0x12, 0x13, 0x20, 0x21):
                dst[x, y] = (40, 140, 60)
            elif t in (0x02, 0x03):
                dst[x, y] = (25, 75, 160)
            else:
                r = (t * 37) & 0xFF
                g = (t * 73) & 0xFF
                b = (t * 17) & 0xFF
                dst[x, y] = (r, g, b)
    return out


def upscale(img: Image.Image, scale: int) -> Image.Image:
    """Nearest‑neighbor upscale by an integer factor (e.g. 16 → 4096×4096)."""
    if scale <= 1:
        return img
    w, h = img.size
    return img.resize((w * scale, h * scale), resample=Image.NEAREST)


# --------------------------- True tileset rendering (optional) ------------ #

def decode_tiles16_from_bytes(raw: bytes) -> List[Image.Image]:
    expected = 512 * 16 * 8
    if len(raw) != expected:
        raise ValueError(f"tiles.16 decompressed size mismatch: got {len(raw)}, expected {expected}")

    tiles: List[Image.Image] = []
    off = 0
    for _ in range(512):
        im = Image.new('RGB', (16, 16))
        px = im.load()
        for y in range(16):
            row = raw[off:off+8]
            off += 8
            x = 0
            for b in row:
                hi = (b >> 4) & 0x0F
                lo = b & 0x0F
                px[x, y] = EGA_PALETTE[hi]
                px[x+1, y] = EGA_PALETTE[lo]
                x += 2
        tiles.append(im)
    return tiles


def load_tiles16_uncompressed(path: Path) -> List[Image.Image]:
    raw = path.read_bytes()
    return decode_tiles16_from_bytes(raw)


def render_truecolor(tile_img_L: Image.Image, tiles16: List[Image.Image]) -> Image.Image:
    """Composite a truecolor world map using a 512‑tile 16‑color tileset."""
    w, h = tile_img_L.size
    out = Image.new('RGB', (w * 16, h * 16))  # 4096×4096 for 256×256 tiles
    for ty in range(h):
        for tx in range(w):
            t = tile_img_L.getpixel((tx, ty)) & 0x1FF  # 0..511
            out.paste(tiles16[t], (tx * 16, ty * 16))
    return out


# --------------------------- CLI ------------------------------------------ #

def main():
    ap = argparse.ArgumentParser(description="Ultima V BRIT.DAT extractor & viewer")
    ap.add_argument('--data-ovl', required=True, type=Path, help='Path to DATA.OVL')
    ap.add_argument('--brit', required=True, type=Path, help='Path to BRIT.DAT')
    ap.add_argument('--out', type=Path, default=Path('brit_quicklook.png'),
                    help='Quick‑look PNG of tile ids colored heuristically')
    ap.add_argument('--scale', type=int, default=16,
                    help='Integer upscale for quick‑look (default 16 → 4096×4096)')
    ap.add_argument('--dump-ids', type=Path, default=None,
                    help='Optional: write raw 8‑bit tile id image (brit_ids.png)')
    ap.add_argument('--tiles16-decompressed', type=Path, default=None,
                    help='Path to *decompressed* tiles.16 blob (65536 bytes)')
    ap.add_argument('--tiles16', type=Path, default=None,
                    help='Path to compressed OR raw tiles.16 (auto-detected)')
    ap.add_argument('--truecolor', type=Path, default=None,
                    help='Write true tiles render PNG (requires tiles.16)')

    args = ap.parse_args()

    # Build tile-id map
    chunk_map = read_chunk_index_table_from_data_ovl(args.data_ovl)
    compact = read_brit_chunks(args.brit)
    tile_ids_img = reconstruct_world(chunk_map, compact)

    # Optional raw ids output
    if args.dump_ids:
        tile_ids_img.save(args.dump_ids)
        print(f"Wrote raw tile id image: {args.dump_ids}")

    # Quick‑look colored map (scaled up by default to 4096×4096)
    quick = colorize_quicklook(tile_ids_img)
    quick_big = upscale(quick, args.scale)
    quick_big.save(args.out)
    print(f"Wrote quick‑look PNG: {args.out} ({quick_big.size[0]}×{quick_big.size[1]})")

    # True tileset render (optional)
    if (args.tiles16 or args.tiles16_decompressed) and args.truecolor:
        if args.tiles16:
            raw = read_lzw_blob_auto(args.tiles16, fallback_expected_len=512*16*8)
            tileset = decode_tiles16_from_bytes(raw)
        else:
            tileset = load_tiles16_uncompressed(args.tiles16_decompressed)
        true_im = render_truecolor(tile_ids_img, tileset)
        true_im.save(args.truecolor)
        print(f"Wrote truecolor map: {args.truecolor} ({true_im.size[0]}×{true_im.size[1]})")
    elif args.truecolor and not (args.tiles16 or args.tiles16_decompressed):
        print("[warn] --truecolor requested but no tiles.16 provided; use --tiles16 or --tiles16-decompressed")


if __name__ == '__main__':
    main()

