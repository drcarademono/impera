#!/usr/bin/env python3
from __future__ import annotations
import sys, struct, os
from dataclasses import dataclass
from pathlib import Path
from typing import List, Optional, Tuple, Protocol

from PySide6 import QtCore, QtGui, QtWidgets

# --------------------- Ultima V formats ---------------------

CHUNK_SIDE = 16
WORLD_CHUNKS = 16
WORLD_SIDE = CHUNK_SIDE * WORLD_CHUNKS
WATER_TILE_ID = 0x01

TILES16_NAME   = "TILES.16"
BRIT_NAME      = "BRIT.DAT"
DATA_OVL_NAME  = "DATA.OVL"

SETTLE_SIDE = 32

# --- CBT constants ---
CBT_SIDE = 11
CBT_BLOCK_SIZE = 0x20
CBT_BLOCKS_PER_MAP = 11
CBT_MAP_SIZE = CBT_BLOCK_SIZE * CBT_BLOCKS_PER_MAP

SETTLEMENT_LAYOUT: dict[str, List[Tuple[str, int]]] = {
    "CASTLE.DAT": [
        ("Lord British's Castle", 5), ("Blackthorn's Castle", 5),
        ("West Britanny", 1), ("North Britanny", 1), ("East Britanny", 1),
        ("Paws", 1), ("Cove", 1), ("Buccaneer's Den", 1),
    ],
    "TOWNE.DAT": [
        ("Moonglow", 2), ("Britain", 2), ("Jhelom", 2), ("Yew", 2),
        ("Minoc", 2), ("Trinsic", 2), ("Skara Brae", 2), ("New Magincia", 2),
    ],
    "DWELLING.DAT": [
        ("Fogsbane", 3), ("Stormcrow", 3), ("Greyhaven", 3), ("Waveguide", 3),
        ("Iolo's hut", 1), ("Spektran", 1), ("Sin'Vraal's hut", 1), ("Grendel's hut", 1),
    ],
    "KEEP.DAT": [
        ("Ararat", 2), ("Bordermarch", 2), ("Farthing", 1), ("Windemere", 1),
        ("Stonegate", 1), ("The Lycaeum", 3), ("Empath Abbey", 3), ("The Serpent's Hold", 3),
    ],
}

def _u8(b: int) -> int: return b & 0xFF
def _s8(b: int) -> int:
    b &= 0xFF
    return b - 256 if b >= 128 else b
def _to_s8(v: int) -> int: return v & 0xFF

def tslot_to_loc_index(tslot: int) -> int:
    return 0 if tslot == 0 else (1 if tslot in (1,3) else 2)

@dataclass
class NPCSchedule:
    ai: List[int]   # 3 bytes
    x: List[int]    # 3 bytes (0..31)
    y: List[int]    # 3 bytes (0..31)
    z: List[int]    # 3 bytes (signed)
    times: List[int]  # 4 bytes (hours)

@dataclass
class NPCInfo:
    schedules: List[NPCSchedule]     # 32 entries
    types: List[int]
    dialogs: List[int]

class NPCManager:
    def __init__(self, path: Path):
        self.path = path
        self.infos: List[NPCInfo] = []
        data = path.read_bytes()
        if len(data) != 4608:
            raise ValueError(f"{path.name} must be 4608 bytes; got {len(data)}")
        off = 0
        for _ in range(8):
            schedules: List[NPCSchedule] = []
            npc_sched_raw = data[off:off+512]; off += 512
            for i in range(32):
                base = i * 16
                ai  = [ _u8(npc_sched_raw[base+0]), _u8(npc_sched_raw[base+1]), _u8(npc_sched_raw[base+2]) ]
                x   = [ _u8(npc_sched_raw[base+3]), _u8(npc_sched_raw[base+4]), _u8(npc_sched_raw[base+5]) ]
                y   = [ _u8(npc_sched_raw[base+6]), _u8(npc_sched_raw[base+7]), _u8(npc_sched_raw[base+8]) ]
                z   = [ _s8(npc_sched_raw[base+9]), _s8(npc_sched_raw[base+10]), _s8(npc_sched_raw[base+11]) ]
                tim = [ _u8(npc_sched_raw[base+12]), _u8(npc_sched_raw[base+13]), _u8(npc_sched_raw[base+14]), _u8(npc_sched_raw[base+15]) ]
                schedules.append(NPCSchedule(ai, x, y, z, tim))
            types = list(data[off:off+32]); off += 32
            dialogs = list(data[off:off+32]); off += 32
            self.infos.append(NPCInfo(schedules, types, dialogs))
        self._dirty = False

    def save(self, out_path: Optional[Path] = None) -> Path:
        out = bytearray()
        for info in self.infos:
            for sch in info.schedules:
                out.extend(bytes([
                    sch.ai[0]&0xFF, sch.ai[1]&0xFF, sch.ai[2]&0xFF,
                    sch.x[0]&0xFF,  sch.x[1]&0xFF,  sch.x[2]&0xFF,
                    sch.y[0]&0xFF,  sch.y[1]&0xFF,  sch.y[2]&0xFF,
                    _to_s8(sch.z[0]), _to_s8(sch.z[1]), _to_s8(sch.z[2]),
                    sch.times[0]&0xFF, sch.times[1]&0xFF, sch.times[2]&0xFF, sch.times[3]&0xFF
                ]))
            out.extend(bytes([t & 0xFF for t in info.types]))
            out.extend(bytes([d & 0xFF for d in info.dialogs]))
        target = out_path or self.path
        Path(target).write_bytes(out)
        self._dirty = False
        return target

    def mark_dirty(self): self._dirty = True
    def is_dirty(self) -> bool: return self._dirty

    def get_dialog(self, map_idx: int, npc_idx: int) -> int:
        return self.infos[map_idx].dialogs[npc_idx] & 0xFF
    def set_dialog(self, map_idx: int, npc_idx: int, value: int):
        self.infos[map_idx].dialogs[npc_idx] = value & 0xFF; self.mark_dirty()

    def get_type(self, map_idx: int, npc_idx: int) -> int:
        return self.infos[map_idx].types[npc_idx] & 0xFF
    def set_type(self, map_idx: int, npc_idx: int, value: int):
        self.infos[map_idx].types[npc_idx] = value & 0xFF; self.mark_dirty()

    def sprite_tile_for_type(self, type_byte: int) -> int:
        return (0x100 + (type_byte & 0xFF)) & 0x1FF

    def get_ai_for_tslot(self, map_idx: int, npc_idx: int, tslot: int) -> int:
        li = tslot_to_loc_index(tslot)
        return self.infos[map_idx].schedules[npc_idx].ai[li] & 0xFF
    def set_ai_for_tslot(self, map_idx: int, npc_idx: int, tslot: int, value: int):
        li = tslot_to_loc_index(tslot)
        self.infos[map_idx].schedules[npc_idx].ai[li] = value & 0xFF; self.mark_dirty()

    def get_xyz_for_tslot(self, map_idx: int, npc_idx: int, tslot: int) -> Tuple[int,int,int]:
        li = tslot_to_loc_index(tslot)
        sch = self.infos[map_idx].schedules[npc_idx]
        return (sch.x[li], sch.y[li], sch.z[li])
    def set_xy_for_tslot(self, map_idx: int, npc_idx: int, tslot: int, x: int, y: int):
        li = tslot_to_loc_index(tslot)
        sch = self.infos[map_idx].schedules[npc_idx]
        sch.x[li] = max(0, min(SETTLE_SIDE-1, x)) & 0xFF
        sch.y[li] = max(0, min(SETTLE_SIDE-1, y)) & 0xFF
        self.mark_dirty()
    def set_z_for_tslot(self, map_idx: int, npc_idx: int, tslot: int, z: int):
        li = tslot_to_loc_index(tslot)
        sch = self.infos[map_idx].schedules[npc_idx]
        z = max(-128, min(127, int(z)))
        sch.z[li] = z
        self.mark_dirty()

# --- LZW (tiles.16) ---

class BitStream:
    def __init__(self, data: bytes, msb_first: bool):
        self.data = data; self.msb_first = msb_first; self.bitpos = 0
    def read(self, nbits: int) -> int:
        acc = 0
        if self.msb_first:
            for _ in range(nbits):
                if self.bitpos // 8 >= len(self.data): raise EOFError
                byte = self.data[self.bitpos // 8]
                shift = 7 - (self.bitpos % 8)
                acc = (acc << 1) | ((byte >> shift) & 1)
                self.bitpos += 1
        else:
            for i in range(nbits):
                if self.bitpos // 8 >= len(self.data): raise EOFError
                byte = self.data[self.bitpos // 8]
                acc |= ((byte >> (self.bitpos % 8)) & 1) << i
                self.bitpos += 1
        return acc

def lzw_decompress_tiles16_giflsb(comp: bytes, expected_len: int) -> bytes:
    min_code_size = 8; clear = 1 << min_code_size; end = clear + 1
    code_size = min_code_size + 1; next_code = end + 1; max_bits = 12
    dict_seq = [bytes([i]) for i in range(clear)] + [b"", b""]
    bs = BitStream(comp, msb_first=False)
    out = bytearray(); prev = None
    def reset():
        nonlocal code_size, next_code, dict_seq, prev
        code_size = min_code_size + 1; next_code = end + 1
        dict_seq = [bytes([i]) for i in range(clear)] + [b"", b""]
        prev = None
    try:
        while True:
            code = bs.read(code_size)
            if code == clear: reset(); continue
            if code == end: break
            if code < len(dict_seq) and dict_seq[code] != b"": entry = dict_seq[code]
            elif prev is not None and code == next_code: entry = prev + prev[:1]
            else: break
            out.extend(entry)
            if expected_len and len(out) >= expected_len:
                out = out[:expected_len]; break
            if prev is not None and next_code < (1 << max_bits):
                dict_seq.append(prev + entry[:1]); next_code += 1
                if next_code == (1 << code_size) and code_size < max_bits: code_size += 1
            prev = entry
    except EOFError:
        pass
    if len(out) != expected_len:
        raise ValueError(f"LZW decode produced {len(out)} bytes; expected {expected_len}")
    return bytes(out)

def read_tiles16(path: Path) -> List[QtGui.QPixmap]:
    raw = path.read_bytes()
    if len(raw) >= 4:
        exp = struct.unpack('<I', raw[:4])[0]
        blob = lzw_decompress_tiles16_giflsb(raw[4:], exp) if 0 < exp <= 8_388_608 else raw
    else:
        blob = raw
    if len(blob) != 512 * 16 * 8:
        raise ValueError(f"tiles.16 decompressed size mismatch: got {len(blob)}, expected 65536")
    palette = [
        (0,0,0),(0,0,170),(0,170,0),(0,170,170),
        (170,0,0),(170,0,170),(170,85,0),(170,170,170),
        (85,85,85),(85,85,255),(85,255,85),(85,255,255),
        (255,85,85),(255,85,255),(255,255,85),(255,255,255),
    ]
    pixmaps: List[QtGui.QPixmap] = []
    off = 0
    for _ in range(512):
        img = QtGui.QImage(16, 16, QtGui.QImage.Format.Format_RGB888)
        for y in range(16):
            row = blob[off:off+8]; off += 8; x = 0
            for b in row:
                hi = (b >> 4) & 0x0F; lo = b & 0x0F
                r,g,b_ = palette[hi]; img.setPixelColor(x, y, QtGui.QColor(r,g,b_)); x += 1
                r,g,b_ = palette[lo]; img.setPixelColor(x, y, QtGui.QColor(r,g,b_)); x += 1
        pixmaps.append(QtGui.QPixmap.fromImage(img))
    return pixmaps

def read_chunk_index_table_from_data_ovl(path: Path) -> List[int]:
    with path.open('rb') as f:
        f.seek(0x3886)
        data = f.read(256)
        if len(data) != 256:
            raise ValueError("DATA.OVL too short when reading chunk table")
        return list(data)

def read_brit_like_chunks(path: Path) -> List[bytes]:
    data = path.read_bytes()
    if len(data) % 256 != 0:
        raise ValueError(f"{path.name} size is not a multiple of 256")
    return [data[i:i+256] for i in range(0, len(data), 256)]

# ------------------------------ Backends ---------------------------------

class MapBackend(Protocol):
    path: Path
    tiles: List[QtGui.QPixmap]
    @property
    def width(self) -> int: ...
    @property
    def height(self) -> int: ...
    @property
    def has_maps(self) -> bool: ...
    @property
    def has_levels(self) -> bool: ...
    @property
    def map_names(self) -> List[str]: ...
    @property
    def level_names(self) -> List[str]: ...
    @property
    def current_map_index(self) -> int: ...
    @property
    def current_level_index(self) -> int: ...
    def select_map(self, idx: int) -> None: ...
    def select_level(self, idx: int) -> None: ...
    def tile_at(self, x: int, y: int) -> int: ...
    def paint_tile(self, x: int, y: int, tile: int) -> None: ...
    def save(self, out_path: Optional[Path] = None) -> Path: ...

# ---- BRIT.DAT ----

@dataclass
class U5World:
    data_ovl: Path
    brit_dat: Path
    tiles16_path: Path
    chunk_map: List[int]
    compact_chunks: List[bytes]
    world: List[int]
    tiles: List[QtGui.QPixmap]

    @classmethod
    def load(cls, data_ovl: Path, brit: Path, tiles16: Path) -> 'U5World':
        chunk_map = read_chunk_index_table_from_data_ovl(data_ovl)
        compact = read_brit_like_chunks(brit)
        world = [0] * (WORLD_SIDE * WORLD_SIDE)
        for cy in range(WORLD_CHUNKS):
            for cx in range(WORLD_CHUNKS):
                i = cy * WORLD_CHUNKS + cx
                idx = chunk_map[i]
                base = (cy * CHUNK_SIDE) * WORLD_SIDE + (cx * CHUNK_SIDE)
                if idx == 0xFF:
                    for ty in range(CHUNK_SIDE):
                        row_off = base + ty * WORLD_SIDE
                        for tx in range(CHUNK_SIDE):
                            world[row_off + tx] = WATER_TILE_ID
                else:
                    if idx >= len(compact):
                        raise IndexError(f"DATA.OVL chunk index {idx} out of range (have {len(compact)})")
                    chunk = compact[idx]; k = 0
                    for ty in range(CHUNK_SIDE):
                        row_off = base + ty * WORLD_SIDE
                        for tx in range(CHUNK_SIDE):
                            world[row_off + tx] = chunk[k]; k += 1
        tiles = read_tiles16(tiles16)
        return cls(data_ovl, brit, tiles16, chunk_map, compact, world, tiles)

    def paint_tile(self, x: int, y: int, tile: int):
        if 0 <= x < WORLD_SIDE and 0 <= y < WORLD_SIDE:
            self.world[y*WORLD_SIDE + x] = tile & 0xFF
    def tile_at(self, x: int, y: int) -> int:
        return self.world[y*WORLD_SIDE + x]
    def save_brit_dat(self, out_path: Optional[Path] = None):
        used = [v for v in self.chunk_map if v != 0xFF]
        if not used: raise ValueError("No non-water chunks found in mapping.")
        max_idx = max(used)
        idx_to_chunk_xy: List[Tuple[int,int]] = [(-1,-1)] * (max_idx+1)
        for cy in range(WORLD_CHUNKS):
            for cx in range(WORLD_CHUNKS):
                v = self.chunk_map[cy*WORLD_CHUNKS + cx]
                if v != 0xFF and v <= max_idx: idx_to_chunk_xy[v] = (cx, cy)
        out = bytearray()
        for m in range(max_idx+1):
            cx, cy = idx_to_chunk_xy[m]
            if cx < 0:
                chunk_bytes = bytes([WATER_TILE_ID] * (CHUNK_SIDE*CHUNK_SIDE))
            else:
                base_x = cx * CHUNK_SIDE; base_y = cy * CHUNK_SIDE
                chunk = bytearray(256); k = 0
                for ty in range(CHUNK_SIDE):
                    row_off = (base_y + ty) * WORLD_SIDE + base_x
                    for tx in range(CHUNK_SIDE):
                        chunk[k] = self.world[row_off + tx]; k += 1
                chunk_bytes = bytes(chunk)
            out += chunk_bytes
        target = out_path or self.brit_dat
        Path(target).write_bytes(out)
        return target

class BritBackend(MapBackend):
    def __init__(self, data_ovl: Path, brit_path: Path, tiles16: Path):
        self.world = U5World.load(data_ovl, brit_path, tiles16)
        self.path = brit_path
        self.tiles = self.world.tiles
    @property
    def width(self) -> int: return WORLD_SIDE
    @property
    def height(self) -> int: return WORLD_SIDE
    @property
    def has_maps(self) -> bool: return False
    @property
    def has_levels(self) -> bool: return False
    @property
    def map_names(self) -> List[str]: return []
    @property
    def level_names(self) -> List[str]: return []
    @property
    def current_map_index(self) -> int: return 0
    @property
    def current_level_index(self) -> int: return 0
    def select_map(self, idx: int) -> None: pass
    def select_level(self, idx: int) -> None: pass
    def tile_at(self, x: int, y: int) -> int: return self.world.tile_at(x,y)
    def paint_tile(self, x: int, y: int, tile: int) -> None: self.world.paint_tile(x,y,tile)
    def save(self, out_path: Optional[Path] = None) -> Path: return self.world.save_brit_dat(out_path)

# ---- UNDER.DAT ----

class UnderBackend(MapBackend):
    def __init__(self, under_path: Path, tiles16: Path):
        self.path = under_path
        self.tiles = read_tiles16(tiles16)
        chunks = read_brit_like_chunks(under_path)
        if len(chunks) < WORLD_CHUNKS * WORLD_CHUNKS:
            raise ValueError("UNDER.DAT does not contain 256 chunks")
        self.world = [0] * (WORLD_SIDE * WORLD_SIDE)
        idx = 0
        for cy in range(WORLD_CHUNKS):
            for cx in range(WORLD_CHUNKS):
                chunk = chunks[idx]; idx += 1
                k = 0
                base = (cy * CHUNK_SIDE) * WORLD_SIDE + (cx * CHUNK_SIDE)
                for ty in range(CHUNK_SIDE):
                    row_off = base + ty * WORLD_SIDE
                    for tx in range(CHUNK_SIDE):
                        self.world[row_off + tx] = chunk[k]; k += 1
    @property
    def width(self) -> int: return WORLD_SIDE
    @property
    def height(self) -> int: return WORLD_SIDE
    @property
    def has_maps(self) -> bool: return False
    @property
    def has_levels(self) -> bool: return False
    @property
    def map_names(self) -> List[str]: return []
    @property
    def level_names(self) -> List[str]: return []
    @property
    def current_map_index(self) -> int: return 0
    @property
    def current_level_index(self) -> int: return 0
    def select_map(self, idx: int) -> None: pass
    def select_level(self, idx: int) -> None: pass
    def tile_at(self, x: int, y: int) -> int: return self.world[y*WORLD_SIDE + x]
    def paint_tile(self, x: int, y: int, tile: int) -> None:
        if 0 <= x < WORLD_SIDE and 0 <= y < WORLD_SIDE:
            self.world[y*WORLD_SIDE + x] = tile & 0xFF
    def save(self, out_path: Optional[Path] = None) -> Path:
        out = bytearray()
        for cy in range(WORLD_CHUNKS):
            for cx in range(WORLD_CHUNKS):
                base = (cy * CHUNK_SIDE) * WORLD_SIDE + (cx * CHUNK_SIDE)
                chunk = bytearray(256); k = 0
                for ty in range(CHUNK_SIDE):
                    row_off = base + ty * WORLD_SIDE
                    for tx in range(CHUNK_SIDE):
                        chunk[k] = self.world[row_off + tx]; k += 1
                out += chunk
        target = out_path or self.path
        Path(target).write_bytes(out)
        return target

# ---- CBT Backend with structured accessors ----

class CBTBackend(MapBackend):
    def __init__(self, path: Path, tiles16: Path):
        self.path = path
        self.tiles = read_tiles16(tiles16)
        blob = path.read_bytes()
        if len(blob) % CBT_MAP_SIZE != 0:
            raise ValueError(f"{path.name} size is not a multiple of {CBT_MAP_SIZE} bytes")
        self._blob = bytearray(blob)
        self._map_count = len(self._blob) // CBT_MAP_SIZE
        self._cur_map = 0

    # MapBackend
    @property
    def width(self) -> int: return CBT_SIDE
    @property
    def height(self) -> int: return CBT_SIDE
    @property
    def has_maps(self) -> bool: return True
    @property
    def has_levels(self) -> bool: return False
    @property
    def map_names(self) -> List[str]: return [f"Map {i}" for i in range(self._map_count)]
    @property
    def level_names(self) -> List[str]: return []
    @property
    def current_map_index(self) -> int: return self._cur_map
    @property
    def current_level_index(self) -> int: return 0
    def select_map(self, idx: int) -> None:
        self._cur_map = max(0, min(self._map_count - 1, int(idx)))
    def select_level(self, idx: int) -> None: pass
    def _map_base(self, map_idx: Optional[int] = None) -> int:
        if map_idx is None: map_idx = self._cur_map
        return map_idx * CBT_MAP_SIZE
    def _row_block_off(self, y: int, map_idx: Optional[int] = None) -> int:
        return self._map_base(map_idx) + (y * CBT_BLOCK_SIZE)
    def tile_at(self, x: int, y: int) -> int:
        if 0 <= x < CBT_SIDE and 0 <= y < CBT_SIDE:
            return self._blob[self._row_block_off(y) + x]
        return 0
    def paint_tile(self, x: int, y: int, tile: int) -> None:
        if 0 <= x < CBT_SIDE and 0 <= y < CBT_SIDE:
            self._blob[self._row_block_off(y) + x] = tile & 0xFF
    def save(self, out_path: Optional[Path] = None) -> Path:
        target = out_path or self.path
        Path(target).write_bytes(self._blob); return target

    # ---- Structured CBT accessors ----
    # Helpers for the 21 special bytes: start at offset row_off + 11
    def _special_off(self, y: int) -> int:
        return self._row_block_off(y) + 11

    # Row 1 (y=0): New tile IDs [8], then 8 unknown, then 5 pad
    def get_new_tile_ids(self) -> List[int]:
        off = self._special_off(0)
        return list(self._blob[off:off+8])
    def set_new_tile_ids(self, ids8: List[int]):
        ids8 = (ids8 + [0]*8)[:8]
        off = self._special_off(0)
        for i,v in enumerate(ids8):
            self._blob[off+i] = v & 0xFF

    # Party positions: rows 2..5 (y=1..4): 6 X, 6 Y, 9 pad
    # Directions: 'E','W','S','N' mapping to rows 1..4
    _dir_to_row = {'E':1, 'W':2, 'S':3, 'N':4}
    def get_party_positions(self, direction: str) -> Tuple[List[int], List[int]]:
        y = self._dir_to_row[direction.upper()]
        off = self._special_off(y)
        xs = list(self._blob[off:off+6])
        ys = list(self._blob[off+6:off+12])
        return xs, ys
    def set_party_positions(self, direction: str, xs: List[int], ys: List[int]):
        y = self._dir_to_row[direction.upper()]
        xs = (xs + [0]*6)[:6]; ys = (ys + [0]*6)[:6]
        off = self._special_off(y)
        for i in range(6): self._blob[off+i] = xs[i] & 0xFF
        for i in range(6): self._blob[off+6+i] = ys[i] & 0xFF

    # Row 6 (y=5): monster tiles (16), then 5 pad
    def get_monster_tiles(self) -> List[int]:
        off = self._special_off(5)
        return list(self._blob[off:off+16])
    def set_monster_tiles(self, tiles16: List[int]):
        tiles16 = (tiles16 + [0]*16)[:16]
        off = self._special_off(5)
        for i,v in enumerate(tiles16): self._blob[off+i] = v & 0xFF

    # Row 7/8 (y=6/7): monster X / Y coords (16), then 5 unknown
    def get_monster_xs(self) -> List[int]:
        off = self._special_off(6); return list(self._blob[off:off+16])
    def set_monster_xs(self, xs16: List[int]):
        xs16 = (xs16 + [0]*16)[:16]
        off = self._special_off(6)
        for i,v in enumerate(xs16): self._blob[off+i] = v & 0xFF
    def get_monster_ys(self) -> List[int]:
        off = self._special_off(7); return list(self._blob[off:off+16])
    def set_monster_ys(self, ys16: List[int]):
        ys16 = (ys16 + [0]*16)[:16]
        off = self._special_off(7)
        for i,v in enumerate(ys16): self._blob[off+i] = v & 0xFF

    # Row 9 (y=8): trigger positions: 8 X, 8 Y, 5 pad
    def get_triggers(self) -> Tuple[List[int], List[int]]:
        off = self._special_off(8)
        xs = list(self._blob[off:off+8])
        ys = list(self._blob[off+8:off+16])
        return xs, ys
    def set_triggers(self, xs8: List[int], ys8: List[int]):
        xs8 = (xs8 + [0]*8)[:8]; ys8 = (ys8 + [0]*8)[:8]
        off = self._special_off(8)
        for i in range(8): self._blob[off+i] = xs8[i] & 0xFF
        for i in range(8): self._blob[off+8+i] = ys8[i] & 0xFF

    # Rows 10/11 (y=9/10): changed-tile positions (8 X, 8 Y, 5 pad)
    def get_change_positions(self, row: int) -> Tuple[List[int], List[int]]:
        assert row in (10,11)
        y = 9 if row == 10 else 10
        off = self._special_off(y)
        xs = list(self._blob[off:off+8]); ys = list(self._blob[off+8:off+16])
        return xs, ys
    def set_change_positions(self, row: int, xs8: List[int], ys8: List[int]):
        assert row in (10,11)
        y = 9 if row == 10 else 10
        xs8 = (xs8 + [0]*8)[:8]; ys8 = (ys8 + [0]*8)[:8]
        off = self._special_off(y)
        for i in range(8): self._blob[off+i]   = xs8[i] & 0xFF
        for i in range(8): self._blob[off+8+i] = ys8[i] & 0xFF

# ---- Settlement Backend ----

class SettlementBackend(MapBackend):
    def __init__(self, path: Path, tiles16: Path):
        self.path = path
        self.tiles = read_tiles16(tiles16)
        self.basename = path.name.upper()
        if self.basename not in SETTLEMENT_LAYOUT:
            raise ValueError(f"No layout metadata for {self.basename}")
        self.layout = SETTLEMENT_LAYOUT[self.basename]
        blob = path.read_bytes()
        bytes_per_level = SETTLE_SIDE * SETTLE_SIDE
        levels_total = sum(levels for _, levels in self.layout)
        expected = levels_total * bytes_per_level
        if len(blob) < expected:
            raise ValueError(f"{self.basename} size mismatch: got {len(blob)} bytes, expected {expected}")
        blob = blob[:expected]
        self._blob = bytearray(blob)
        self.offsets: List[List[int]] = []
        off = 0
        for _map_name, lvl_count in self.layout:
            levels = []
            for _ in range(lvl_count):
                levels.append(off); off += bytes_per_level
            self.offsets.append(levels)
        self._cur_map = 0
        self._cur_level = 0
        self._level_views: List[List[memoryview]] = []
        mv = memoryview(self._blob)
        for levels in self.offsets:
            row = [mv[o:o+bytes_per_level] for o in levels]
            self._level_views.append(row)
        npc_path = path.with_suffix(".NPC")
        self.npc_mgr: Optional[NPCManager] = NPCManager(npc_path) if npc_path.exists() else None

    @property
    def width(self) -> int: return SETTLE_SIDE
    @property
    def height(self) -> int: return SETTLE_SIDE
    @property
    def has_maps(self) -> bool: return True
    @property
    def has_levels(self) -> bool: return True
    @property
    def map_names(self) -> List[str]: return [name for name, _ in self.layout]
    @property
    def level_names(self) -> List[str]:
        return [f"Level {i}" for i in range(len(self.offsets[self._cur_map]))]
    @property
    def current_map_index(self) -> int: return self._cur_map
    @property
    def current_level_index(self) -> int: return self._cur_level
    def select_map(self, idx: int) -> None:
        idx = max(0, min(idx, len(self.offsets)-1)); self._cur_map = idx; self._cur_level = 0
    def select_level(self, idx: int) -> None:
        lvls = len(self.offsets[self._cur_map]); idx = max(0, min(idx, lvls-1)); self._cur_level = idx
    def _cur_level_view(self) -> memoryview:
        return self._level_views[self._cur_map][self._cur_level]
    def tile_at(self, x: int, y: int) -> int:
        if 0 <= x < SETTLE_SIDE and 0 <= y < SETTLE_SIDE:
            return self._cur_level_view()[y*SETTLE_SIDE + x]
        return 0
    def paint_tile(self, x: int, y: int, tile: int) -> None:
        if 0 <= x < SETTLE_SIDE and 0 <= y < SETTLE_SIDE:
            self._cur_level_view()[y*SETTLE_SIDE + x] = tile & 0xFF
    def save(self, out_path: Optional[Path] = None) -> Path:
        target = out_path or self.path
        Path(target).write_bytes(self._blob)
        if self.npc_mgr and self.npc_mgr.is_dirty(): self.npc_mgr.save()
        return target

# ------------------------------ Widgets ---------------------------------

class TilePalette(QtWidgets.QWidget):
    tile_selected = QtCore.Signal(int)
    def __init__(self, tiles: List[QtGui.QPixmap], scale: int = 2, parent=None):
        super().__init__(parent)
        self.tiles = tiles; self.scale = max(1, min(4, int(scale)))
        self.sel = 0x10; self.border = 2; self.margin = 6; self.cols = 1
        sp = self.sizePolicy(); sp.setHorizontalPolicy(QtWidgets.QSizePolicy.Policy.Expanding); sp.setVerticalPolicy(QtWidgets.QSizePolicy.Policy.Minimum)
        self.setSizePolicy(sp); self.setMouseTracking(True)
        self._recompute_metrics(); self._reflow_to_width(self.width())
    def _recompute_metrics(self):
        self.tile_draw_px = 16 * self.scale; self.cell = self.tile_draw_px + 2 * self.border
    def set_scale(self, scale: int):
        new_scale = max(1, min(4, int(scale)))
        if new_scale != self.scale:
            self.scale = new_scale; self._recompute_metrics(); self._reflow_to_width(self.width()); self.updateGeometry(); self.update()
    def _reflow_to_width(self, w: int):
        usable = max(0, w - 2 * self.margin); cols = max(1, usable // self.cell)
        if cols != self.cols: self.cols = cols; self.updateGeometry(); self.update()
    def resizeEvent(self, ev: QtGui.QResizeEvent):
        self._reflow_to_width(ev.size().width()); super().resizeEvent(ev)
    def sizeHint(self):
        rows = (len(self.tiles) + self.cols - 1) // self.cols
        width = 2 * self.margin + max(1, self.cols) * self.cell
        height = 2 * self.margin + max(1, rows) * self.cell
        return QtCore.QSize(width, height)
    def minimumSizeHint(self): return QtCore.QSize(160, 2 * self.margin + self.cell * 4)
    def paintEvent(self, ev):
        p = QtGui.QPainter(self); p.fillRect(self.rect(), QtGui.QColor(30,30,30))
        for i in range(len(self.tiles)):
            r = i // self.cols; c = i % self.cols
            x = self.margin + c * self.cell; y = self.margin + r * self.cell
            p.fillRect(x, y, self.cell-1, self.cell-1, QtGui.QColor(50,50,50))
            target = QtCore.QRect(x + self.border, y + self.border, self.tile_draw_px, self.tile_draw_px)
            p.drawPixmap(target, self.tiles[i])
            if i == self.sel:
                pen = QtGui.QPen(QtGui.QColor(255,255,0), 2); p.setPen(pen); p.drawRect(target.adjusted(-1, -1, 1, 1))
    def mousePressEvent(self, ev):
        if ev.button() == QtCore.Qt.MouseButton.LeftButton:
            idx = self._index_at(ev.position().toPoint())
            if idx is not None:
                self.sel = idx; self.tile_selected.emit(idx); self.update()
    def _index_at(self, pos: QtCore.QPoint) -> Optional[int]:
        x = pos.x() - self.margin; y = pos.y() - self.margin
        if x < 0 or y < 0: return None
        c = x // self.cell; r = y // self.cell
        if c < 0 or r < 0: return None
        idx = r * self.cols + c
        return idx if 0 <= idx < len(self.tiles) else None

class NPCInspector(QtWidgets.QWidget):
    dialog_changed = QtCore.Signal(int)
    ai_changed = QtCore.Signal(int)
    level_changed = QtCore.Signal(int)
    prev_type = QtCore.Signal()
    next_type = QtCore.Signal()
    def __init__(self, tiles: List[QtGui.QPixmap], parent=None):
        super().__init__(parent)
        self.tiles = tiles; self._sprite_tile = 256; self._has_selection = False
        self._x = self._y = self._z = 0; self._level_count = 1
        self._build_ui()
    def _build_ui(self):
        lay = QtWidgets.QVBoxLayout(self); lay.setContentsMargins(8,8,8,8)
        title = QtWidgets.QLabel("<b>NPC Inspector</b>"); lay.addWidget(title)
        self.sel_label = QtWidgets.QLabel("No NPC selected"); lay.addWidget(self.sel_label)
        sprite_row = QtWidgets.QHBoxLayout()
        self.btn_prev = QtWidgets.QToolButton(); self.btn_prev.setText("◀"); self.btn_prev.clicked.connect(lambda: self.prev_type.emit())
        self.btn_next = QtWidgets.QToolButton(); self.btn_next.setText("▶"); self.btn_next.clicked.connect(lambda: self.next_type.emit())
        self.sprite_view = QtWidgets.QLabel(); self.sprite_view.setFixedSize(48,48)
        self.sprite_view.setFrameShape(QtWidgets.QFrame.Shape.Box); self.sprite_view.setAlignment(QtCore.Qt.AlignmentFlag.AlignCenter)
        sprite_row.addWidget(self.btn_prev); sprite_row.addWidget(self.sprite_view, 1); sprite_row.addWidget(self.btn_next)
        lay.addLayout(sprite_row)
        form = QtWidgets.QFormLayout()
        self.sp_type = QtWidgets.QSpinBox(); self.sp_type.setRange(0, 255); self.sp_type.setReadOnly(True)
        form.addRow("Type byte (sprite)", self.sp_type)
        self.sp_dialog = QtWidgets.QSpinBox(); self.sp_dialog.setRange(0, 255); self.sp_dialog.valueChanged.connect(self.dialog_changed.emit)
        form.addRow("Dialog #", self.sp_dialog)
        self.sp_ai = QtWidgets.QSpinBox(); self.sp_ai.setRange(0, 255); self.sp_ai.valueChanged.connect(self.ai_changed.emit)
        form.addRow("AI byte (current t)", self.sp_ai)
        self.level_combo = QtWidgets.QComboBox(); self.level_combo.currentIndexChanged.connect(self._emit_level_changed)
        form.addRow("Level", self.level_combo)
        self.lbl_pos = QtWidgets.QLabel("X: -, Y: -, Z: -"); form.addRow("Position", self.lbl_pos)
        lay.addLayout(form); lay.addStretch(1); self.setEnabled(False)
    def set_selection(self, npc_index: Optional[int]):
        self._has_selection = npc_index is not None; self.setEnabled(self._has_selection)
        self.sel_label.setText(f"Selected NPC: {npc_index}" if npc_index is not None else "No NPC selected")
    def set_type(self, type_byte: int):
        self.sp_type.blockSignals(True); self.sp_type.setValue(type_byte & 0xFF); self.sp_type.blockSignals(False)
    def set_dialog(self, dialog_number: int):
        self.sp_dialog.blockSignals(True); self.sp_dialog.setValue(dialog_number & 0xFF); self.sp_dialog.blockSignals(False)
    def set_ai(self, ai_byte: int):
        self.sp_ai.blockSignals(True); self.sp_ai.setValue(ai_byte & 0xFF); self.sp_ai.blockSignals(False)
    def set_level_options(self, level_count: int):
        level_count = max(1, int(level_count))
        if level_count != self._level_count or self.level_combo.count() != level_count:
            self._level_count = level_count
            self.level_combo.blockSignals(True); self.level_combo.clear()
            for i in range(level_count): self.level_combo.addItem(str(i), i)
            self.level_combo.blockSignals(False)
    def set_level_value(self, z_level: int):
        if self.level_combo.count() == 0: return
        z_level = max(0, min(self.level_combo.count()-1, int(z_level)))
        if self.level_combo.currentIndex() != z_level:
            self.level_combo.blockSignals(True); self.level_combo.setCurrentIndex(z_level); self.level_combo.blockSignals(False)
    def set_xyz(self, x: int, y: int, z: int):
        self._x, self._y, self._z = x, y, z; self.lbl_pos.setText(f"X: {x}, Y: {y}, Z: {z}")
    def set_sprite_tile(self, tile_index: int):
        self._sprite_tile = tile_index & 0x1FF
        pm = self.tiles[self._sprite_tile]
        pm_scaled = pm.scaled(48, 48, QtCore.Qt.AspectRatioMode.IgnoreAspectRatio,
                              QtCore.Qt.TransformationMode.FastTransformation)
        self.sprite_view.setPixmap(pm_scaled)
    def _emit_level_changed(self, idx: int):
        if idx < 0: return
        self.level_changed.emit(idx)

# ---- CBT Editor Panel ----

class _ArrayEditor(QtWidgets.QWidget):
    """A small helper to edit a linear array of bytes with fixed length."""
    values_changed = QtCore.Signal(list)
    def __init__(self, count: int, columns: int, label_prefix: str, max_value: int = 255, parent=None):
        super().__init__(parent)
        self.count = count; self.columns = columns; self.maxv = max_value
        grid = QtWidgets.QGridLayout(self); grid.setContentsMargins(0,0,0,0); grid.setHorizontalSpacing(6); grid.setVerticalSpacing(4)
        self.spins: List[QtWidgets.QSpinBox] = []
        for i in range(count):
            sp = QtWidgets.QSpinBox(); sp.setRange(0, max_value); sp.setFixedWidth(60)
            sp.valueChanged.connect(self._emit)
            self.spins.append(sp)
            r = i // columns; c = i % columns
            grid.addWidget(QtWidgets.QLabel(f"{label_prefix}{i:02d}"), r*2, c)
            grid.addWidget(sp, r*2+1, c)
    def set_values(self, vals: List[int]):
        vals = (vals + [0]*self.count)[:self.count]
        for i,v in enumerate(vals):
            self.spins[i].blockSignals(True); self.spins[i].setValue(int(v) & 0xFF); self.spins[i].blockSignals(False)
    def values(self) -> List[int]: return [sp.value() for sp in self.spins]
    def _emit(self, _=None): self.values_changed.emit(self.values())

class _PairsEditor(QtWidgets.QWidget):
    """Edit N pairs of (X,Y) as two parallel rows of spinboxes."""
    pairs_changed = QtCore.Signal(list, list)
    def __init__(self, count: int, columns: int, parent=None):
        super().__init__(parent)
        self.count = count; self.columns = columns
        grid = QtWidgets.QGridLayout(self); grid.setContentsMargins(0,0,0,0); grid.setHorizontalSpacing(6); grid.setVerticalSpacing(4)
        self.sp_x: List[QtWidgets.QSpinBox] = []; self.sp_y: List[QtWidgets.QSpinBox] = []
        for i in range(count):
            sx = QtWidgets.QSpinBox(); sx.setRange(0, 10); sx.setFixedWidth(60)
            sy = QtWidgets.QSpinBox(); sy.setRange(0, 10); sy.setFixedWidth(60)
            sx.valueChanged.connect(self._emit); sy.valueChanged.connect(self._emit)
            self.sp_x.append(sx); self.sp_y.append(sy)
            r = i // columns; c = i % columns
            grid.addWidget(QtWidgets.QLabel(f"{i:02d}"), r*3, c)
            grid.addWidget(QtWidgets.QLabel("X"), r*3+1, c)
            grid.addWidget(sx, r*3+2, c)
            grid.addWidget(QtWidgets.QLabel("Y"), r*3+3, c)
            grid.addWidget(sy, r*3+4, c)
    def set_pairs(self, xs: List[int], ys: List[int]):
        xs = (xs + [0]*self.count)[:self.count]; ys = (ys + [0]*self.count)[:self.count]
        for i in range(self.count):
            self.sp_x[i].blockSignals(True); self.sp_y[i].blockSignals(True)
            self.sp_x[i].setValue(int(xs[i]) & 0xFF); self.sp_y[i].setValue(int(ys[i]) & 0xFF)
            self.sp_x[i].blockSignals(False); self.sp_y[i].blockSignals(False)
    def pairs(self) -> Tuple[List[int], List[int]]:
        return [sx.value() for sx in self.sp_x], [sy.value() for sy in self.sp_y]
    def _emit(self, _=None):
        xs, ys = self.pairs(); self.pairs_changed.emit(xs, ys)

class CBTPanel(QtWidgets.QScrollArea):
    """Side panel for CBT editing."""
    def __init__(self, backend_getter, parent=None):
        super().__init__(parent)
        self.backend_getter = backend_getter
        self.setWidgetResizable(True)
        wrap = QtWidgets.QWidget(); self.setWidget(wrap)
        v = QtWidgets.QVBoxLayout(wrap); v.setContentsMargins(8,8,8,8); v.setSpacing(12)

        # New tile IDs (Row 1 specials)
        box_new = QtWidgets.QGroupBox("New Tile IDs (Row 1 specials, 8 bytes)")
        new_layout = QtWidgets.QVBoxLayout(box_new)
        self.ed_new_ids = _ArrayEditor(count=8, columns=8, label_prefix="T", max_value=255)
        self.ed_new_ids.values_changed.connect(self._apply_new_ids)
        new_layout.addWidget(self.ed_new_ids)
        v.addWidget(box_new)

        # Party spawns
        box_party = QtWidgets.QGroupBox("Party Spawns (6 X + 6 Y each)")
        party_layout = QtWidgets.QFormLayout(box_party)
        self.ed_e = _PairsEditor(count=6, columns=6)
        self.ed_w = _PairsEditor(count=6, columns=6)
        self.ed_s = _PairsEditor(count=6, columns=6)
        self.ed_n = _PairsEditor(count=6, columns=6)
        self.ed_e.pairs_changed.connect(lambda xs,ys: self._apply_party('E', xs, ys))
        self.ed_w.pairs_changed.connect(lambda xs,ys: self._apply_party('W', xs, ys))
        self.ed_s.pairs_changed.connect(lambda xs,ys: self._apply_party('S', xs, ys))
        self.ed_n.pairs_changed.connect(lambda xs,ys: self._apply_party('N', xs, ys))
        party_layout.addRow("From East", self.ed_e)
        party_layout.addRow("From West", self.ed_w)
        party_layout.addRow("From South/Below", self.ed_s)
        party_layout.addRow("From North/Above", self.ed_n)
        v.addWidget(box_party)

        # Monsters
        box_mon = QtWidgets.QGroupBox("Monsters (16)")
        mon_layout = QtWidgets.QFormLayout(box_mon)
        self.ed_mon_tiles = _ArrayEditor(count=16, columns=16, label_prefix="M", max_value=255)
        self.ed_mon_x = _ArrayEditor(count=16, columns=16, label_prefix="X", max_value=10)
        self.ed_mon_y = _ArrayEditor(count=16, columns=16, label_prefix="Y", max_value=10)
        self.ed_mon_tiles.values_changed.connect(self._apply_mon_tiles)
        self.ed_mon_x.values_changed.connect(self._apply_mon_x)
        self.ed_mon_y.values_changed.connect(self._apply_mon_y)
        mon_layout.addRow("Tiles", self.ed_mon_tiles)
        mon_layout.addRow("X Coords", self.ed_mon_x)
        mon_layout.addRow("Y Coords", self.ed_mon_y)
        v.addWidget(box_mon)

        # Triggers
        box_trig = QtWidgets.QGroupBox("Triggers (Row 9): 8 (X,Y) positions")
        self.ed_triggers = _PairsEditor(count=8, columns=8)
        self.ed_triggers.pairs_changed.connect(self._apply_triggers)
        lay_trig = QtWidgets.QVBoxLayout(box_trig); lay_trig.addWidget(self.ed_triggers)
        v.addWidget(box_trig)

        # Change positions rows 10 & 11
        box_change = QtWidgets.QGroupBox("Changed-tile Positions (Rows 10 & 11): 8 (X,Y) each")
        lay_change = QtWidgets.QFormLayout(box_change)
        self.ed_ch10 = _PairsEditor(count=8, columns=8); self.ed_ch11 = _PairsEditor(count=8, columns=8)
        self.ed_ch10.pairs_changed.connect(lambda xs,ys: self._apply_change(10, xs, ys))
        self.ed_ch11.pairs_changed.connect(lambda xs,ys: self._apply_change(11, xs, ys))
        lay_change.addRow("Row 10 targets", self.ed_ch10)
        lay_change.addRow("Row 11 targets", self.ed_ch11)
        v.addWidget(box_change)

        v.addStretch(1)

    def refresh_from_backend(self):
        b: CBTBackend = self.backend_getter()
        if not isinstance(b, CBTBackend): return
        self.ed_new_ids.set_values(b.get_new_tile_ids())
        for d, editor in (('E',self.ed_e),('W',self.ed_w),('S',self.ed_s),('N',self.ed_n)):
            xs, ys = b.get_party_positions(d); editor.set_pairs(xs, ys)
        self.ed_mon_tiles.set_values(b.get_monster_tiles())
        self.ed_mon_x.set_values(b.get_monster_xs())
        self.ed_mon_y.set_values(b.get_monster_ys())
        xs, ys = b.get_triggers(); self.ed_triggers.set_pairs(xs, ys)
        xs10, ys10 = b.get_change_positions(10); self.ed_ch10.set_pairs(xs10, ys10)
        xs11, ys11 = b.get_change_positions(11); self.ed_ch11.set_pairs(xs11, ys11)

    # Apply handlers
    def _apply_new_ids(self, vals: List[int]):
        b: CBTBackend = self.backend_getter()
        if isinstance(b, CBTBackend): b.set_new_tile_ids(vals)
    def _apply_party(self, d: str, xs: List[int], ys: List[int]):
        b: CBTBackend = self.backend_getter()
        if isinstance(b, CBTBackend): b.set_party_positions(d, xs, ys)
    def _apply_mon_tiles(self, vals: List[int]):
        b: CBTBackend = self.backend_getter()
        if isinstance(b, CBTBackend): b.set_monster_tiles(vals)
    def _apply_mon_x(self, vals: List[int]):
        b: CBTBackend = self.backend_getter()
        if isinstance(b, CBTBackend): b.set_monster_xs(vals)
    def _apply_mon_y(self, vals: List[int]):
        b: CBTBackend = self.backend_getter()
        if isinstance(b, CBTBackend): b.set_monster_ys(vals)
    def _apply_triggers(self, xs: List[int], ys: List[int]):
        b: CBTBackend = self.backend_getter()
        if isinstance(b, CBTBackend): b.set_triggers(xs, ys)
    def _apply_change(self, row: int, xs: List[int], ys: List[int]):
        b: CBTBackend = self.backend_getter()
        if isinstance(b, CBTBackend): b.set_change_positions(row, xs, ys)

# ---- Map Canvas ----

class MapCanvas(QtWidgets.QGraphicsView):
    status_changed = QtCore.Signal(str)
    npc_selected = QtCore.Signal(int)
    def __init__(self, backend: MapBackend, parent=None):
        super().__init__(parent)
        self.setViewportUpdateMode(QtWidgets.QGraphicsView.ViewportUpdateMode.FullViewportUpdate)
        self.setRenderHint(QtGui.QPainter.RenderHint.Antialiasing, False)
        self.setRenderHint(QtGui.QPainter.RenderHint.SmoothPixmapTransform, False)
        self.setDragMode(QtWidgets.QGraphicsView.DragMode.NoDrag)
        self.setTransformationAnchor(self.ViewportAnchor.NoAnchor)
        self.setResizeAnchor(self.ViewportAnchor.NoAnchor)

        self.backend = backend
        self.scene = QtWidgets.QGraphicsScene(self)
        self.setScene(self.scene)

        self.tile_px = 16
        self.map_pixmap = QtGui.QPixmap(self.backend.width*self.tile_px, self.backend.height*self.tile_px)
        self.map_pixmap.fill(QtGui.QColor(0,0,0))
        self.map_item = self.scene.addPixmap(self.map_pixmap)

        self.current_tile = 0x10
        self._painting = False
        self._panning_with_space = False
        self._pan = False
        self._pan_start = QtCore.QPoint()

        self._undo_stack: list[list[tuple[int,int,int,int]]] = []
        self._redo_stack: list[list[tuple[int,int,int,int]]] = []
        self._current_stroke: list[tuple[int,int,int,int]] = []
        self._current_stroke_seen: set[tuple[int,int]] = set()
        self.brush_diameter = 1  # 1..5

        # NPC overlay
        self.npc_mode = False
        self.tslot = 1
        self.npc_selection: Optional[int] = None
        self._npc_dragging = False
        self._npc_drag_idx: Optional[int] = None
        self._npc_rects: List[Tuple[int, QtCore.QRectF]] = []

        self.redraw_entire_map()

    def settlement_npc_mgr(self) -> Optional[NPCManager]:
        if isinstance(self.backend, SettlementBackend):
            return self.backend.npc_mgr
        return None

    def set_backend(self, backend: MapBackend):
        self.backend = backend
        self.resetTransform()
        self._undo_stack.clear(); self._redo_stack.clear()
        self._current_stroke.clear(); self._current_stroke_seen.clear()
        self.map_pixmap = QtGui.QPixmap(self.backend.width*self.tile_px, self.backend.height*self.tile_px)
        self.map_pixmap.fill(QtGui.QColor(0,0,0))
        self.map_item.setPixmap(self.map_pixmap)
        self.npc_selection = None; self._npc_rects.clear()
        self.redraw_entire_map()

    def _set_tile_no_record(self, x: int, y: int, tile: int):
        self.backend.paint_tile(x, y, tile)
        painter = QtGui.QPainter(self.map_pixmap)
        painter.drawPixmap(x*self.tile_px, y*self.tile_px, self.backend.tiles[tile & 0x1FF])
        painter.end()

    def _record_change(self, x: int, y: int, old_tile: int, new_tile: int):
        key = (x, y)
        if key in self._current_stroke_seen: return
        if old_tile != new_tile:
            self._current_stroke.append((x, y, old_tile, new_tile))
            self._current_stroke_seen.add(key)

    def _begin_stroke(self):
        self._current_stroke = []
        self._current_stroke_seen = set()

    def _commit_stroke(self):
        if self._current_stroke:
            self._undo_stack.append(self._current_stroke)
            self._redo_stack.clear()
        self._current_stroke = []
        self._current_stroke_seen = set()

    def undo(self):
        if not self._undo_stack: return
        stroke = self._undo_stack.pop()
        for (x, y, old_tile, _new_tile) in reversed(stroke):
            self._set_tile_no_record(x, y, old_tile)
        self._redo_stack.append(stroke)
        self.map_item.setPixmap(self.map_pixmap.copy())

    def redo(self):
        if not self._redo_stack: return
        stroke = self._redo_stack.pop()
        for (x, y, _old_tile, new_tile) in stroke:
            self._set_tile_no_record(x, y, new_tile)
        self._undo_stack.append(stroke)
        self.map_item.setPixmap(self.map_pixmap.copy())

    def _paint_with_brush(self, cx: int, cy: int):
        if self.npc_mode: return
        d = max(1, min(5, int(self.brush_diameter)))
        r = (d - 1) / 2.0; r2 = r * r; r_int = int(r) + 1
        changed = False
        for dy in range(-r_int-1, r_int+2):
            for dx in range(-r_int-1, r_int+2):
                if (dx*dx + dy*dy) > r2 + 1e-9: continue
                x = cx + dx; y = cy + dy
                if 0 <= x < self.backend.width and 0 <= y < self.backend.height:
                    old_tile = self.backend.tile_at(x, y)
                    new_tile = self.current_tile & 0xFF
                    if old_tile != new_tile:
                        self._record_change(x, y, old_tile, new_tile)
                        self._set_tile_no_record(x, y, new_tile); changed = True
        if changed: self.map_item.setPixmap(self.map_pixmap.copy())

    def set_current_tile(self, idx: int):
        self.current_tile = idx; self.status_changed.emit(f"Selected tile: {idx}")

    def set_npc_mode(self, enabled: bool):
        self.npc_mode = bool(enabled)
        if not self.npc_mode: self.npc_selection = None
        self.redraw_entire_map()

    def set_tslot(self, tslot: int):
        self.tslot = max(0, min(3, int(tslot)))
        self.redraw_entire_map()
        if self.npc_selection is not None: self.npc_selected.emit(self.npc_selection)

    def redraw_entire_map(self):
        painter = QtGui.QPainter(self.map_pixmap)
        for y in range(self.backend.height):
            for x in range(self.backend.width):
                t = self.backend.tile_at(x,y) & 0x1FF
                painter.drawPixmap(x*self.tile_px, y*self.tile_px, self.backend.tiles[t])
        painter.end()
        self.map_item.setPixmap(self.map_pixmap.copy())
        self._draw_npcs_overlay()

    def _draw_npcs_overlay(self):
        self._npc_rects.clear()
        if not self.npc_mode: return
        npc_mgr = self.settlement_npc_mgr()
        if not npc_mgr or not isinstance(self.backend, SettlementBackend): return
        pm = self.map_pixmap.copy(); p = QtGui.QPainter(pm)
        map_idx = self.backend.current_map_index; lvl_idx = self.backend.current_level_index
        info = npc_mgr.infos[map_idx]; li = tslot_to_loc_index(self.tslot)
        def is_empty_slot(nidx: int) -> bool:
            sch = info.schedules[nidx]; t_byte = info.types[nidx] & 0xFF; d_byte = info.dialogs[nidx] & 0xFF
            return (t_byte == 0 and d_byte == 0 and (not any(sch.x)) and (not any(sch.y)))
        for npc_idx in range(1, 32):
            if is_empty_slot(npc_idx): continue
            sch = info.schedules[npc_idx]; x, y, z = sch.x[li], sch.y[li], sch.z[li]
            if z != lvl_idx: continue
            type_byte = info.types[npc_idx] & 0xFF; tile_index = npc_mgr.sprite_tile_for_type(type_byte)
            px = x * self.tile_px; py = y * self.tile_px
            p.drawPixmap(px, py, self.backend.tiles[tile_index])
            rect = QtCore.QRectF(px, py, self.tile_px, self.tile_px); self._npc_rects.append((npc_idx, rect))
            if self.npc_selection == npc_idx:
                pen = QtGui.QPen(QtGui.QColor(255, 230, 0), 2); p.setPen(pen); p.drawRect(rect.adjusted(1,1,-1,-1))
        p.end(); self.map_item.setPixmap(pm)

    def _scene_to_tile(self, scene_pos: QtCore.QPointF) -> Tuple[int,int]:
        x = int(scene_pos.x()) // self.tile_px; y = int(scene_pos.y()) // self.tile_px
        return x, y
    def _hit_test_npc(self, pos: QtCore.QPointF) -> Optional[int]:
        for (idx, r) in reversed(self._npc_rects):
            if r.contains(pos): return idx
        return None

    def keyPressEvent(self, ev: QtGui.QKeyEvent):
        if ev.key() == QtCore.Qt.Key.Key_Space:
            self._panning_with_space = True; self.setCursor(QtCore.Qt.CursorShape.OpenHandCursor)
        super().keyPressEvent(ev)
    def keyReleaseEvent(self, ev: QtGui.QKeyEvent):
        if ev.key() == QtCore.Qt.Key.Key_Space:
            self._panning_with_space = False
            if not self._pan: self.setCursor(QtCore.Qt.CursorShape.ArrowCursor)
        super().keyReleaseEvent(ev)

    def mousePressEvent(self, ev: QtGui.QMouseEvent):
        if ev.button() == QtCore.Qt.MouseButton.MiddleButton or (
            ev.button() == QtCore.Qt.MouseButton.LeftButton and self._panning_with_space
        ):
            self._pan = True; self._pan_start = ev.position().toPoint()
            self.setCursor(QtCore.Qt.CursorShape.ClosedHandCursor); return
        p = self.mapToScene(ev.position().toPoint())
        if self.npc_mode:
            if ev.button() == QtCore.Qt.MouseButton.LeftButton:
                idx = self._hit_test_npc(p)
                if idx is not None:
                    self.npc_selection = idx; self._npc_dragging = True; self._npc_drag_idx = idx
                    self.npc_selected.emit(idx); return
                else:
                    self.npc_selection = None; self._npc_dragging = False; self._npc_drag_idx = None
                    self._draw_npcs_overlay(); return
            super().mousePressEvent(ev); return
        if ev.button() == QtCore.Qt.MouseButton.LeftButton and (
            ev.modifiers() & QtCore.Qt.KeyboardModifier.ControlModifier
        ):
            x, y = self._scene_to_tile(p)
            if 0 <= x < self.backend.width and 0 <= y < self.backend.height:
                self.set_current_tile(self.backend.tile_at(x, y) & 0x1FF)
            return
        if ev.button() == QtCore.Qt.MouseButton.LeftButton:
            self._painting = True; self._begin_stroke()
            x,y = self._scene_to_tile(p)
            if 0 <= x < self.backend.width and 0 <= y < self.backend.height:
                self._paint_with_brush(x, y); self.status_changed.emit(f"Painted {self.current_tile} at {x},{y}")
            return
        super().mousePressEvent(ev)

    def mouseMoveEvent(self, ev: QtGui.QMouseEvent):
        p = self.mapToScene(ev.position().toPoint()); x,y = self._scene_to_tile(p)
        if 0 <= x < self.backend.width and 0 <= y < self.backend.height:
            t = self.backend.tile_at(x,y); self.status_changed.emit(f"Tile {x},{y} = {t}")
        if self._pan:
            delta = ev.position().toPoint() - self._pan_start; self._pan_start = ev.position().toPoint()
            self.horizontalScrollBar().setValue(self.horizontalScrollBar().value() - delta.x())
            self.verticalScrollBar().setValue(self.verticalScrollBar().value() - delta.y()); return
        if self.npc_mode and self._npc_dragging and (ev.buttons() & QtCore.Qt.MouseButton.LeftButton):
            npc_mgr = self.settlement_npc_mgr()
            if npc_mgr and isinstance(self.backend, SettlementBackend) and self._npc_drag_idx is not None:
                tx, ty = max(0, min(self.backend.width-1, x)), max(0, min(self.backend.height-1, y))
                npc_mgr.set_xy_for_tslot(self.backend.current_map_index, self._npc_drag_idx, self.tslot, tx, ty)
                self._draw_npcs_overlay()
            return
        if self._painting and (ev.buttons() & QtCore.Qt.MouseButton.LeftButton):
            if 0 <= x < self.backend.width and 0 <= y < self.backend.height:
                self._paint_with_brush(x, y); return
        super().mouseMoveEvent(ev)

    def mouseReleaseEvent(self, ev: QtGui.QMouseEvent):
        if ev.button() == QtCore.Qt.MouseButton.MiddleButton or (
            ev.button() == QtCore.Qt.MouseButton.LeftButton and self._pan
        ):
            self._pan = False
            self.setCursor(QtCore.Qt.CursorShape.OpenHandCursor if self._panning_with_space else QtCore.Qt.CursorShape.ArrowCursor)
            return
        if ev.button() == QtCore.Qt.MouseButton.LeftButton:
            if self.npc_mode and self._npc_dragging:
                self._npc_dragging = False; self._npc_drag_idx = None; return
            if self._painting:
                self._painting = False; self._commit_stroke(); return
        super().mouseReleaseEvent(ev)

    def wheelEvent(self, ev: QtGui.QWheelEvent):
        if ev.modifiers() & QtCore.Qt.KeyboardModifier.ControlModifier:
            old_pos = self.mapToScene(ev.position().toPoint())
            factor = 1.25 if ev.angleDelta().y() > 0 else 0.8
            self.scale(factor, factor); new_pos = self.mapToScene(ev.position().toPoint())
            delta = new_pos - old_pos; self.translate(delta.x(), delta.y())
        else:
            super().wheelEvent(ev)

# -------------------------------- Main Window --------------------------------

class MainWindow(QtWidgets.QMainWindow):
    MODE_TILES = 0
    MODE_NPC   = 1
    MODE_CBT   = 2

    def __init__(self, backend: MapBackend, palette_scale: int, data_ovl: Optional[Path]):
        super().__init__()
        self.setWindowTitle("Ultima V – Map Editor")
        self.backend = backend
        self.data_ovl = data_ovl
        self.mode = self.MODE_TILES

        # Canvas
        self.canvas = MapCanvas(backend)
        self.canvas.status_changed.connect(self.statusBar().showMessage)
        self.canvas.npc_selected.connect(self._on_npc_selected)

        # Right side panel as a stacked widget (Tiles / NPC / CBT)
        self.side_stack = QtWidgets.QStackedWidget()

        # Tiles panel
        self.palette = TilePalette(self.backend.tiles[:256], scale=palette_scale)
        self.palette.tile_selected.connect(self.canvas.set_current_tile)
        pal_wrap = QtWidgets.QScrollArea(); pal_wrap.setWidgetResizable(True); pal_wrap.setFrameShape(QtWidgets.QFrame.Shape.NoFrame)
        pal_wrap.setWidget(self.palette)

        # NPC panel
        self.npc_inspector = NPCInspector(self.backend.tiles)
        self.npc_inspector.dialog_changed.connect(self._on_dialog_changed)
        self.npc_inspector.ai_changed.connect(self._on_ai_changed)
        self.npc_inspector.prev_type.connect(self._on_prev_type)
        self.npc_inspector.next_type.connect(self._on_next_type)
        self.npc_inspector.level_changed.connect(self._on_npc_level_changed)

        # CBT panel
        self.cbt_panel = CBTPanel(lambda: self.backend if isinstance(self.backend, CBTBackend) else None)

        self.side_stack.addWidget(pal_wrap)          # index 0
        self.side_stack.addWidget(self.npc_inspector) # index 1
        self.side_stack.addWidget(self.cbt_panel)     # index 2

        splitter = QtWidgets.QSplitter()
        splitter.addWidget(self.canvas)
        splitter.addWidget(self.side_stack)
        splitter.setStretchFactor(0, 1); splitter.setStretchFactor(1, 0)
        self.setCentralWidget(splitter)

        self._build_toolbar(palette_scale)
        self._setup_map_level_ui()
        self._update_mode_availability()
        self._activate_initial_mode()

        self.resize(1600, 950)

    def _build_toolbar(self, palette_scale: int):
        act_open = QtGui.QAction("Open…", self); act_open.setShortcut(QtGui.QKeySequence("Ctrl+O")); act_open.triggered.connect(self.open_file); self.addAction(act_open)
        act_save = QtGui.QAction("Save", self); act_save.setShortcut(QtGui.QKeySequence("Ctrl+S")); act_save.triggered.connect(self.save_current); self.addAction(act_save)
        tb = self.addToolBar("Main"); tb.addAction(act_open); tb.addAction(act_save)

        tb.addSeparator(); tb.addWidget(QtWidgets.QLabel("Palette: "))
        self.scale_combo = QtWidgets.QComboBox(); self.scale_combo.addItems(["1×","2×","3×","4×"])
        initial_index = max(0, min(3, palette_scale-1)); self.scale_combo.setCurrentIndex(initial_index)
        self.scale_combo.currentIndexChanged.connect(self._on_scale_changed); tb.addWidget(self.scale_combo)

        tb.addSeparator()
        act_undo = QtGui.QAction("Undo", self); act_undo.setShortcut(QtGui.QKeySequence("Ctrl+Z")); act_undo.triggered.connect(self.canvas.undo); self.addAction(act_undo); tb.addAction(act_undo)
        act_redo = QtGui.QAction("Redo", self); act_redo.setShortcuts([QtGui.QKeySequence("Ctrl+Y"), QtGui.QKeySequence("Shift+Ctrl+Z")]); act_redo.triggered.connect(self.canvas.redo); self.addAction(act_redo); tb.addAction(act_redo)

        tb.addSeparator(); tb.addWidget(QtWidgets.QLabel("Brush: "))
        self.brush_combo = QtWidgets.QComboBox(); self.brush_combo.addItems(["1","2","3","4","5"])
        self.brush_combo.setCurrentIndex(0); self.brush_combo.currentTextChanged.connect(self._on_brush_changed); tb.addWidget(self.brush_combo)

        tb.addSeparator(); tb.addWidget(QtWidgets.QLabel("Map: "))
        self.map_combo = QtWidgets.QComboBox(); self.map_combo.currentIndexChanged.connect(self._on_map_changed); tb.addWidget(self.map_combo)
        tb.addWidget(QtWidgets.QLabel("Level: "))
        self.level_combo = QtWidgets.QComboBox(); self.level_combo.currentIndexChanged.connect(self._on_level_changed); tb.addWidget(self.level_combo)

        tb.addSeparator()
        # Mode buttons (exclusive)
        self.btn_tiles = QtWidgets.QToolButton(); self.btn_tiles.setText("Tiles"); self.btn_tiles.setCheckable(True)
        self.btn_npc   = QtWidgets.QToolButton(); self.btn_npc.setText("NPC"); self.btn_npc.setCheckable(True)
        self.btn_cbt   = QtWidgets.QToolButton(); self.btn_cbt.setText("CBT"); self.btn_cbt.setCheckable(True)
        grp = QtWidgets.QButtonGroup(self); grp.setExclusive(True)
        grp.addButton(self.btn_tiles, self.MODE_TILES); grp.addButton(self.btn_npc, self.MODE_NPC); grp.addButton(self.btn_cbt, self.MODE_CBT)
        self.btn_tiles.toggled.connect(lambda on: on and self._switch_mode(self.MODE_TILES))
        self.btn_npc.toggled.connect(lambda on: on and self._switch_mode(self.MODE_NPC))
        self.btn_cbt.toggled.connect(lambda on: on and self._switch_mode(self.MODE_CBT))
        tb.addWidget(self.btn_tiles); tb.addWidget(self.btn_npc); tb.addWidget(self.btn_cbt)

        tb.addWidget(QtWidgets.QLabel("  t:"))
        self.tslot_combo = QtWidgets.QComboBox(); self.tslot_combo.addItems(["t0","t1","t2","t3"])
        self.tslot_combo.setCurrentIndex(1); self.tslot_combo.currentIndexChanged.connect(self._on_tslot_changed); tb.addWidget(self.tslot_combo)

    def _setup_map_level_ui(self):
        self.map_combo.blockSignals(True); self.level_combo.blockSignals(True)
        self.map_combo.clear(); self.level_combo.clear()
        if self.backend.has_maps:
            self.map_combo.addItems(self.backend.map_names); self.map_combo.setEnabled(True)
            self.map_combo.setCurrentIndex(self.backend.current_map_index)
        else:
            self.map_combo.setEnabled(False)
        if self.backend.has_levels:
            self.level_combo.addItems(self.backend.level_names); self.level_combo.setEnabled(True)
            self.level_combo.setCurrentIndex(self.backend.current_level_index)
        else:
            self.level_combo.setEnabled(False)
        self.map_combo.blockSignals(False); self.level_combo.blockSignals(False)

    def _update_mode_availability(self):
        # Tiles always available
        self.btn_tiles.setEnabled(True)
        # NPC available only for SettlementBackend with NPCs
        npc_ok = isinstance(self.backend, SettlementBackend) and self.backend.npc_mgr is not None
        self.btn_npc.setEnabled(npc_ok)
        # CBT available only for CBTBackend
        cbt_ok = isinstance(self.backend, CBTBackend)
        self.btn_cbt.setEnabled(cbt_ok)
        # t-slot only matters for NPC mode
        self.tslot_combo.setEnabled(npc_ok)

    def _activate_initial_mode(self):
        if isinstance(self.backend, CBTBackend):
            self.btn_cbt.setChecked(True)   # auto CBT
        else:
            self.btn_tiles.setChecked(True) # default tiles

    def _switch_mode(self, mode: int):
        self.mode = mode
        if mode == self.MODE_TILES:
            self.canvas.set_npc_mode(False); self.side_stack.setCurrentIndex(0)
        elif mode == self.MODE_NPC:
            self.canvas.set_npc_mode(True);  self.side_stack.setCurrentIndex(1)
        elif mode == self.MODE_CBT:
            self.canvas.set_npc_mode(False); self.side_stack.setCurrentIndex(2)
            self.cbt_panel.refresh_from_backend()

    # Toolbar handlers
    def _on_scale_changed(self, idx: int): self.palette.set_scale(idx + 1)
    def _on_brush_changed(self, txt: str):
        try:
            self.canvas.brush_diameter = max(1, min(5, int(txt)))
            self.statusBar().showMessage(f"Brush diameter: {self.canvas.brush_diameter}", 2000)
        except ValueError:
            pass
    def _on_map_changed(self, idx: int):
        if not self.backend.has_maps: return
        self.backend.select_map(idx)
        self.level_combo.blockSignals(True); self.level_combo.clear()
        if self.backend.has_levels:
            self.level_combo.addItems(self.backend.level_names)
            self.level_combo.setCurrentIndex(self.backend.current_level_index)
        self.level_combo.blockSignals(False)
        self.canvas.redraw_entire_map()
        self.canvas._undo_stack.clear(); self.canvas._redo_stack.clear()
        self.canvas.npc_selection = None; self.npc_inspector.set_selection(None)
        if isinstance(self.backend, CBTBackend) and self.mode == self.MODE_CBT:
            self.cbt_panel.refresh_from_backend()
    def _on_level_changed(self, idx: int):
        if not self.backend.has_levels: return
        self.backend.select_level(idx)
        self.canvas.redraw_entire_map()
        self.canvas._undo_stack.clear(); self.canvas._redo_stack.clear()
        self.canvas.npc_selection = None; self.npc_inspector.set_selection(None)
    def _on_tslot_changed(self, idx: int):
        self.canvas.set_tslot(idx)
        if self.canvas.npc_selection is not None:
            self._populate_inspector(self.canvas.npc_selection)

    def open_file(self):
        fname, _ = QtWidgets.QFileDialog.getOpenFileName(
            self, "Open U5 Map", os.getcwd(),
            "DAT/CBT files (*.DAT *.dat *.CBT *.cbt);;All files (*)"
        )
        if not fname: return
        path = Path(fname)
        try:
            tiles16_path = Path(TILES16_NAME)
            backend = self._make_backend_for_path(path, tiles16_path)
        except Exception as e:
            QtWidgets.QMessageBox.critical(self, "Open failed", str(e)); return
        self.backend = backend
        self.canvas.set_backend(backend)
        self.palette.tiles = backend.tiles[:256]
        self.npc_inspector.tiles = backend.tiles
        self.npc_inspector.set_sprite_tile(256)
        self.palette.updateGeometry(); self.palette.update()
        self._setup_map_level_ui()
        self._update_mode_availability()
        self._activate_initial_mode()
        if isinstance(self.backend, CBTBackend) and self.mode == self.MODE_CBT:
            self.cbt_panel.refresh_from_backend()
        self.statusBar().showMessage(f"Opened: {path}", 4000)

    def _make_backend_for_path(self, path: Path, tiles16: Path) -> MapBackend:
        name = path.name.upper()
        if name == "BRIT.DAT":
            if not self.data_ovl or not self.data_ovl.exists():
                candidate = path.with_name(DATA_OVL_NAME)
                if candidate.exists(): self.data_ovl = candidate
                else: raise RuntimeError("DATA.OVL not found next to BRIT.DAT (required).")
            return BritBackend(self.data_ovl, path, tiles16)
        if name == "UNDER.DAT":
            return UnderBackend(path, tiles16)
        if name.endswith(".CBT"):
            return CBTBackend(path, tiles16)
        if name in SETTLEMENT_LAYOUT or any(name.endswith(k) for k in SETTLEMENT_LAYOUT):
            return SettlementBackend(path, tiles16)
        raise ValueError(f"Unsupported file: {name}")

    def save_current(self):
        try:
            outp = self.backend.save()
            if isinstance(self.backend, SettlementBackend) and self.backend.npc_mgr:
                self.statusBar().showMessage(f"Saved: {outp} and {self.backend.path.with_suffix('.NPC').name}", 4000)
            else:
                self.statusBar().showMessage(f"Saved: {outp}", 4000)
        except Exception as e:
            QtWidgets.QMessageBox.critical(self, "Save failed", str(e))

    # NPC inspector wiring
    def _on_npc_selected(self, npc_idx: int): self._populate_inspector(npc_idx)
    def _populate_inspector(self, npc_idx: int):
        self.npc_inspector.set_selection(npc_idx)
        npc_mgr = self.backend.npc_mgr if isinstance(self.backend, SettlementBackend) else None
        if not npc_mgr: return
        m = self.backend.current_map_index
        t = npc_mgr.get_type(m, npc_idx); self.npc_inspector.set_type(t)
        self.npc_inspector.set_sprite_tile(npc_mgr.sprite_tile_for_type(t))
        d = npc_mgr.get_dialog(m, npc_idx); self.npc_inspector.set_dialog(d)
        ai = npc_mgr.get_ai_for_tslot(m, npc_idx, self.canvas.tslot); self.npc_inspector.set_ai(ai)
        x, y, z = npc_mgr.get_xyz_for_tslot(m, npc_idx, self.canvas.tslot)
        self.npc_inspector.set_xyz(x, y, z)
        level_count = len(self.backend.level_names) if self.backend.has_levels else 1
        self.npc_inspector.set_level_options(level_count)
        self.npc_inspector.set_level_value(z if 0 <= z < level_count else 0)
    def _on_dialog_changed(self, value: int):
        if self.canvas.npc_selection is None: return
        if not isinstance(self.backend, SettlementBackend) or not self.backend.npc_mgr: return
        m = self.backend.current_map_index; i = self.canvas.npc_selection
        self.backend.npc_mgr.set_dialog(m, i, value)
    def _on_ai_changed(self, value: int):
        if self.canvas.npc_selection is None: return
        if not isinstance(self.backend, SettlementBackend) or not self.backend.npc_mgr: return
        m = self.backend.current_map_index; i = self.canvas.npc_selection
        self.backend.npc_mgr.set_ai_for_tslot(m, i, self.canvas.tslot, value)
        self.statusBar().showMessage(f"AI byte set to {value} for NPC {i} at t{self.canvas.tslot}", 2000)
    def _on_prev_type(self): self._step_type(-1)
    def _on_next_type(self): self._step_type(+1)
    def _step_type(self, delta: int):
        if self.canvas.npc_selection is None: return
        if not isinstance(self.backend, SettlementBackend) or not self.backend.npc_mgr: return
        npc_mgr = self.backend.npc_mgr; m = self.backend.current_map_index; i = self.canvas.npc_selection
        cur = npc_mgr.get_type(m, i); newv = (cur + delta) & 0xFF
        npc_mgr.set_type(m, i, newv)
        self.npc_inspector.set_type(newv); self.npc_inspector.set_sprite_tile(npc_mgr.sprite_tile_for_type(newv))
        self.canvas._draw_npcs_overlay()
    def _on_npc_level_changed(self, new_level: int):
        if self.canvas.npc_selection is None: return
        if not isinstance(self.backend, SettlementBackend) or not self.backend.npc_mgr: return
        m = self.backend.current_map_index; i = self.canvas.npc_selection
        self.backend.npc_mgr.set_z_for_tslot(m, i, self.canvas.tslot, new_level)
        x, y, z = self.backend.npc_mgr.get_xyz_for_tslot(m, i, self.canvas.tslot)
        self.npc_inspector.set_xyz(x, y, z); self.canvas._draw_npcs_overlay()

# --------------------------------- Entry ------------------------------------

def main():
    import argparse
    ap = argparse.ArgumentParser(description="Ultima V – Map Editor (Tiles / NPC / CBT)")
    ap.add_argument("--palette-scale", type=int, default=2, help="Palette tile scale: 1..4 (default 2)")
    ap.add_argument("--data-ovl", type=Path, help="Path to DATA.OVL (optional; default ./DATA.OVL)")
    ap.add_argument("--tiles16", type=Path, help="Path to TILES.16 (optional; default ./TILES.16)")
    args = ap.parse_args()

    app = QtWidgets.QApplication(sys.argv)

    tiles16_path = args.tiles16 if args.tiles16 else Path(TILES16_NAME)
    if not tiles16_path.exists():
        QtWidgets.QMessageBox.critical(None, "Startup error", f"{tiles16_path.name} not found ({tiles16_path})"); sys.exit(1)

    brit_path = Path(BRIT_NAME)
    if not brit_path.exists():
        QtWidgets.QMessageBox.critical(None, "Startup error", f"{BRIT_NAME} not found in current directory."); sys.exit(1)

    data_ovl = args.data_ovl if args.data_ovl else brit_path.with_name(DATA_OVL_NAME)
    if not data_ovl.exists():
        QtWidgets.QMessageBox.critical(None, "Startup error", f"{DATA.OVL_NAME} not found (looked at {data_ovl})."); sys.exit(1)

    try:
        backend: MapBackend = BritBackend(data_ovl, brit_path, tiles16_path)
    except Exception as e:
        QtWidgets.QMessageBox.critical(None, "Startup error", str(e)); sys.exit(1)

    win = MainWindow(backend, palette_scale=max(1, min(4, args.palette_scale)), data_ovl=data_ovl)
    win.show()
    sys.exit(app.exec())

if __name__ == "__main__":
    main()

