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
from u5_formats import lzw_decode, lzw_encode, read_tiles_blob
import argparse
import struct
from pathlib import Path
from typing import List, Tuple, Optional

from PIL import Image  # pillow

# --- LZW: GIF-like (works for Ultima V TILES.16) -------------------------- #

def lzw_decompress_tiles16_giflsb(comp: bytes, expected_len: int) -> bytes:
    return lzw_decode(comp, expected_len)

def lzw_decompress_auto(comp: bytes, expected_len: int) -> bytes:
    return lzw_decode(comp, expected_len)

def read_lzw_blob_auto(path: Path, fallback_expected_len: Optional[int] = None) -> bytes:
    if fallback_expected_len == 65536:
        return read_tiles_blob(path)
    raw = path.read_bytes()
    if len(raw) < 4:
        raise ValueError("Resource header truncated")
    return lzw_decode(raw[4:], struct.unpack_from("<I", raw)[0])

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

