#!/usr/bin/env python3
# u5_any16_tool.py — Ultima V PC *.16 importer/exporter (exact DOS format)
# Requires: PySide6
#
# • On-disk format: <u32 uncompressed_len_bytes> + LZW (LSB-first)
# • Decoder tries early-change (GIF standard) then late-change; encoder uses early-change
# • Supports TILES.16 and all multi-image *.16 (TEXT.16, DNG*.16, STORY*.16, ITEMS.16, MON*.16…)

from __future__ import annotations
import sys, struct, math, re
from dataclasses import dataclass
from pathlib import Path
from typing import List, Optional, Tuple

from PySide6 import QtCore, QtGui, QtWidgets

# ============================ Palette & constants ============================

EGA16 = [
    (0,0,0),(0,0,170),(0,170,0),(0,170,170),
    (170,0,0),(170,0,170),(170,85,0),(170,170,170),
    (85,85,85),(85,85,255),(85,255,85),(85,255,255),
    (255,85,85),(255,85,255),(255,255,85),(255,255,255),
]
PAL_Q = [QtGui.QColor(r,g,b) for (r,g,b) in EGA16]
PAL_IDX = { (r,g,b): i for i,(r,g,b) in enumerate(EGA16) }

TILE_W = 16
TILE_H = 16
TILES_COUNT = 512
TILES_BYTES_PER_ROW = 8
TILES_RAW = TILES_COUNT * TILE_H * TILES_BYTES_PER_ROW  # 65536

def bytes_per_row_16(w: int) -> int:
    # Rows padded to 8-pixel (4-byte) boundary for 16-color data
    return ((w + 7) // 8) * 4

def mask_len(w: int, h: int) -> int:
    return (w * h + 7) // 8

# ================================ Bit I/O =====================================

class BitReaderLSB:
    def __init__(self, data: bytes):
        self.data = data
        self.bitpos = 0
    def read(self, nbits: int) -> int:
        acc = 0
        for i in range(nbits):
            bi = self.bitpos >> 3
            if bi >= len(self.data):
                raise EOFError
            acc |= ((self.data[bi] >> (self.bitpos & 7)) & 1) << i
            self.bitpos += 1
        return acc

class BitWriterLSB:
    def __init__(self):
        self.buf = 0
        self.nbits = 0
        self.out = bytearray()
    def write(self, code: int, width: int):
        self.buf |= (code & ((1 << width) - 1)) << self.nbits
        self.nbits += width
        while self.nbits >= 8:
            self.out.append(self.buf & 0xFF)
            self.buf >>= 8
            self.nbits -= 8
    def finish(self) -> bytes:
        if self.nbits:
            self.out.append(self.buf & 0xFF)
            self.buf = 0
            self.nbits = 0
        return bytes(self.out)

# =============================== LZW core =====================================

def _lzw_decode_variant(comp: bytes, expected_len: int, *, early_change: bool) -> bytes:
    MIN=8; CLEAR=1<<MIN; END=CLEAR+1; MAXBITS=12
    br = BitReaderLSB(comp)
    dict_seq = [bytes([i]) for i in range(256)] + [b"", b""]
    code_size = MIN+1
    next_code = END+1
    out = bytearray()
    prev = None

    def reset():
        nonlocal code_size, next_code, prev, dict_seq
        code_size = MIN+1
        next_code = END+1
        prev = None
        dict_seq = [bytes([i]) for i in range(256)] + [b"", b""]

    try:
        while True:
            code = br.read(code_size)
            if code == CLEAR:
                reset(); continue
            if code == END:
                break
            if code < len(dict_seq) and dict_seq[code] != b"":
                entry = dict_seq[code]
            elif prev is not None and code == next_code:
                entry = prev + prev[:1]
            else:
                break
            out.extend(entry)
            if len(out) >= expected_len:
                return bytes(out[:expected_len])
            if prev is not None and next_code < (1 << MAXBITS):
                dict_seq.append(prev + entry[:1])
                next_code += 1
                if early_change:
                    if next_code == (1 << code_size) and code_size < MAXBITS:
                        code_size += 1
                else:
                    if next_code > (1 << code_size) and code_size < MAXBITS:
                        code_size += 1
            prev = entry
    except EOFError:
        pass
    return bytes(out)

def lzw_decode_flexible(header_len_bytes: int, comp: bytes) -> bytes:
    # Try early-change with header length first (matches PC files), then late-change.
    for ec in (True, False):
        blob = _lzw_decode_variant(comp, header_len_bytes, early_change=ec)
        if len(blob) == header_len_bytes:
            return blob
    # As a last-resort fallback, try header*256 (old repacks)
    guess = header_len_bytes * 256
    for ec in (True, False):
        blob = _lzw_decode_variant(comp, guess, early_change=ec)
        if len(blob) == guess:
            return blob
    raise ValueError(f"LZW decode failed for both early/late-change (tried {header_len_bytes} and {guess} bytes)")

def lzw_encode_earlychange(raw: bytes) -> bytes:
    if not raw:
        return b""
    MIN=8; CLEAR=1<<MIN; END=CLEAR+1; MAXBITS=12
    dict_map = { bytes([i]): i for i in range(256) }
    next_code = END+1
    code_size = MIN+1
    bw = BitWriterLSB()
    bw.write(CLEAR, code_size)  # initial CLEAR
    w = b""
    for b in raw:
        k = bytes([b])
        wk = w + k
        if wk in dict_map:
            w = wk; continue
        bw.write(dict_map[w], code_size)
        if next_code < (1 << MAXBITS):
            dict_map[wk] = next_code
            next_code += 1
            if next_code == (1 << code_size) and code_size < MAXBITS:  # early-change
                code_size += 1
        else:
            bw.write(CLEAR, code_size)  # reset dict
            dict_map = { bytes([i]): i for i in range(256) }
            next_code = END+1
            code_size = MIN+1
        w = k
    if w:
        bw.write(dict_map[w], code_size)
    bw.write(END, code_size)
    return bw.finish()

# =========================== In-memory representation ==========================

@dataclass
class ImageSlot:
    width: int
    height: int
    qimage: QtGui.QImage        # ARGB32 (alpha used as mask in 16-bit-offset files)
    mask: Optional[bytes] = None

@dataclass
class U5File:
    path: Optional[Path]
    kind: str                    # "tiles" or "multi"
    count: int = 0
    offsets_16bit: bool = False  # True for ITEMS/MON* variant with mask
    slots: List[Optional[ImageSlot]] = None

# =============================== Decode files =================================

def read_any16(path: Path) -> U5File:
    data = path.read_bytes()
    if len(data) < 4:
        raise ValueError("File too small")
    uncompressed = struct.unpack("<I", data[:4])[0]
    comp = data[4:]
    blob = lzw_decode_flexible(uncompressed, comp)

    # TILES.16 special-case (size + name)
    if path.name.upper() == "TILES.16" and len(blob) == TILES_RAW:
        slots: List[ImageSlot] = []
        off = 0
        for _ in range(TILES_COUNT):
            img = QtGui.QImage(TILE_W, TILE_H, QtGui.QImage.Format.Format_ARGB32)
            for y in range(TILE_H):
                row = blob[off:off+TILES_BYTES_PER_ROW]; off += TILES_BYTES_PER_ROW
                x = 0
                for b in row:
                    hi = (b >> 4) & 0xF   # high nibble = first pixel
                    lo = b & 0xF
                    img.setPixelColor(x, y, PAL_Q[hi]); x += 1
                    img.setPixelColor(x, y, PAL_Q[lo]); x += 1
            slots.append(ImageSlot(TILE_W, TILE_H, img))
        return U5File(path, "tiles", count=TILES_COUNT, offsets_16bit=False, slots=slots)

    # Otherwise: multi-image container
    if len(blob) < 2:
        raise ValueError("Corrupt container")
    count = struct.unpack_from("<H", blob, 0)[0]

    # Parse both 32-bit and 16-bit offset tables, prefer the one that looks sane.
    off32 = [struct.unpack_from("<I", blob, 2+4*i)[0] for i in range(count)] if len(blob) >= 2+4*count else []
    ok32  = len(off32)==count and all(o==0 or (o < len(blob)) for o in off32)

    off16 = [struct.unpack_from("<H", blob, 2+2*i)[0] for i in range(count)] if len(blob) >= 2+2*count else []
    ok16  = len(off16)==count and all(o==0 or (o < len(blob)) for o in off16)

    # Heuristic: ITEMS/MON* commonly 16-bit; otherwise 32-bit. But trust "ok" first.
    name = path.name.upper()
    prefer16 = bool(re.match(r"(ITEMS|MON[0-9A-Z]).*\.16$", name))
    use16 = (ok16 and not ok32) or (ok16 and ok32 and prefer16)  # otherwise 32-bit

    offsets = off16 if use16 else off32
    slots: List[Optional[ImageSlot]] = [None]*count

    def read_image_at(off: int) -> Tuple[ImageSlot, int]:
        w  = struct.unpack_from("<H", blob, off)[0]
        h  = struct.unpack_from("<H", blob, off+2)[0]
        rb = bytes_per_row_16(w)
        start = off + 4
        end   = start + rb*h
        if end > len(blob):
            raise ValueError("Truncated image data")
        img = QtGui.QImage(w, h, QtGui.QImage.Format.Format_ARGB32)
        p = start
        for y in range(h):
            row = blob[p:p+rb]; p += rb
            x = 0
            for b in row:
                hi = (b >> 4) & 0xF
                lo = b & 0xF
                if x < w: img.setPixelColor(x, y, PAL_Q[hi]); x += 1
                if x < w: img.setPixelColor(x, y, PAL_Q[lo]); x += 1
        mask = None
        # 16-bit-offset variant may have mask block immediately after pixels
        if use16 and end + 4 <= len(blob):
            mw = struct.unpack_from("<H", blob, end)[0]
            mh = struct.unpack_from("<H", blob, end+2)[0]
            mbytes = mask_len(mw, mh)
            ms = end + 4; me = ms + mbytes
            if mw == w and mh == h and me <= len(blob):
                mask = blob[ms:me]
                # apply mask as alpha (1 = masked)
                bit = 0x80; bi = 0
                for yy in range(h):
                    for xx in range(w):
                        masked = (mask[bi] & bit) != 0
                        c = QtGui.QColor(img.pixelColor(xx, yy))
                        c.setAlpha(0 if masked else 255)
                        img.setPixelColor(xx, yy, c)
                        bit >>= 1
                        if bit == 0:
                            bit = 0x80; bi += 1
                end = me  # advance consumer pointer
        return ImageSlot(w,h,img,mask), end

    for i, off in enumerate(offsets):
        if off == 0: continue
        slot, _ = read_image_at(off)
        slots[i] = slot

    return U5File(path, "multi", count=count, offsets_16bit=use16, slots=slots)

# =============================== Build blobs ==================================

def build_tiles_blob(f: U5File) -> bytes:
    assert f.kind == "tiles" and len(f.slots) == TILES_COUNT
    out = bytearray(TILES_RAW)
    off = 0
    for s in f.slots:
        img = s.qimage
        if img.width()!=TILE_W or img.height()!=TILE_H:
            raise ValueError("All tiles must be 16×16")
        for y in range(TILE_H):
            for x in range(0, TILE_W, 2):
                c0 = img.pixelColor(x, y);  k0 = (c0.red(), c0.green(), c0.blue())
                c1 = img.pixelColor(x+1, y);k1 = (c1.red(), c1.green(), c1.blue())
                if k0 not in PAL_IDX or k1 not in PAL_IDX:
                    raise ValueError(f"Non-EGA color at ({x},{y})")
                out[off] = ((PAL_IDX[k0] & 0xF) << 4) | (PAL_IDX[k1] & 0xF)
                off += 1
    return bytes(out)

def build_multi_blob(f: U5File) -> bytes:
    assert f.kind == "multi"
    count = f.count
    # Header area: count + offsets
    header = bytearray(2 + (2 if f.offsets_16bit else 4)*count)
    struct.pack_into("<H", header, 0, count)
    body = bytearray()
    offsets = [0]*count

    def append_u16(v: int): body.extend(struct.pack("<H", v))

    def encode_mask_from_alpha(img: QtGui.QImage) -> bytes:
        w, h = img.width(), img.height()
        m = bytearray(mask_len(w,h))
        bit = 0x80; bi = 0
        for y in range(h):
            for x in range(w):
                if img.pixelColor(x,y).alpha() == 0:
                    m[bi] |= bit
                bit >>= 1
                if bit == 0: bit = 0x80; bi += 1
        return bytes(m)

    def append_slot(slot: ImageSlot) -> int:
        start = len(header) + len(body)
        w, h = slot.width, slot.height
        rb = bytes_per_row_16(w)
        append_u16(w); append_u16(h)
        for y in range(h):
            x = 0
            for _ in range(rb):
                # two pixels per byte, hi nibble then low nibble
                if x < w:
                    k0 = slot.qimage.pixelColor(x,y); x0 = (k0.red(),k0.green(),k0.blue())
                    if x0 not in PAL_IDX: raise ValueError(f"Non-EGA color at ({x},{y})")
                    hi = PAL_IDX[x0] & 0xF
                else:
                    hi = 0
                x += 1
                if x < w:
                    k1 = slot.qimage.pixelColor(x,y); x1 = (k1.red(),k1.green(),k1.blue())
                    if x1 not in PAL_IDX: raise ValueError(f"Non-EGA color at ({x-1},{y})")
                    lo = PAL_IDX[x1] & 0xF
                else:
                    lo = 0
                x += 1
                body.append((hi<<4)|lo)
        # Mask block only for 16-bit-offset containers
        if f.offsets_16bit:
            m = slot.mask if slot.mask is not None else encode_mask_from_alpha(slot.qimage)
            append_u16(w); append_u16(h); body.extend(m)
        return start

    for i, s in enumerate(f.slots):
        if s is None: continue
        offsets[i] = append_slot(s)

    if f.offsets_16bit:
        for i, off in enumerate(offsets):
            struct.pack_into("<H", header, 2+2*i, off & 0xFFFF)
    else:
        for i, off in enumerate(offsets):
            struct.pack_into("<I", header, 2+4*i, off)

    return bytes(header + body)

# =============================== PNG helpers ==================================

def tiles_to_sheet(f: U5File) -> QtGui.QImage:
    sheet = QtGui.QImage(32*TILE_W, 16*TILE_H, QtGui.QImage.Format.Format_ARGB32)
    sheet.fill(QtGui.QColor(0,0,0,0))
    p = QtGui.QPainter(sheet)
    for i, s in enumerate(f.slots):
        r, c = divmod(i, 32)
        p.drawImage(QtCore.QPoint(c*TILE_W, r*TILE_H), s.qimage)
    p.end()
    return sheet

def sheet_to_tiles(sheet: QtGui.QImage) -> List[ImageSlot]:
    if sheet.width()!=32*TILE_W or sheet.height()!=16*TILE_H:
        raise ValueError("Spritesheet must be 512×256 px (32×16 tiles).")
    slots = []
    for r in range(16):
        for c in range(32):
            img = sheet.copy(QtCore.QRect(c*TILE_W, r*TILE_H, TILE_W, TILE_H)).convertToFormat(QtGui.QImage.Format.Format_ARGB32)
            for y in range(TILE_H):
                for x in range(TILE_W):
                    key = (img.pixelColor(x,y).red(), img.pixelColor(x,y).green(), img.pixelColor(x,y).blue())
                    if key not in PAL_IDX:
                        raise ValueError(f"Non-EGA color at tile {r*32+c} ({x},{y})")
            slots.append(ImageSlot(TILE_W, TILE_H, img))
    return slots

def export_folder_multi(f: U5File, out_dir: Path):
    out_dir.mkdir(parents=True, exist_ok=True)
    for i, s in enumerate(f.slots):
        if s is None: continue
        (out_dir / f"img_{i:03d}.png")
        s.qimage.save(str(out_dir / f"img_{i:03d}.png"), "PNG")

def import_folder_multi(f: U5File, in_dir: Path) -> List[Optional[ImageSlot]]:
    slots: List[Optional[ImageSlot]] = [None]*f.count
    for i in range(f.count):
        p = in_dir / f"img_{i:03d}.png"
        if not p.exists():
            slots[i] = f.slots[i]  # keep original if absent
            continue
        img = QtGui.QImage(str(p)).convertToFormat(QtGui.QImage.Format.Format_ARGB32)
        if img.isNull():
            raise ValueError(f"Failed to read {p.name}")
        orig = f.slots[i]
        if orig is not None and (img.width()!=orig.width or img.height()!=orig.height):
            raise ValueError(f"{p.name}: size {img.width()}x{img.height()} != expected {orig.width}x{orig.height}")
        # Validate palette (allow alpha 0 for mask in 16-bit-offset variant)
        for y in range(img.height()):
            for x in range(img.width()):
                c = img.pixelColor(x,y)
                if c.alpha()==0 and f.offsets_16bit:  # transparency becomes mask
                    continue
                if (c.red(),c.green(),c.blue()) not in PAL_IDX:
                    raise ValueError(f"{p.name}: Non-EGA color at ({x},{y})")
        slots[i] = ImageSlot(img.width(), img.height(), img)
    return slots

# =================================== GUI ======================================

class Tool(QtWidgets.QWidget):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("Ultima V *.16 Tool (PC)")
        self.resize(980, 680)

        self.ed_path = QtWidgets.QLineEdit()
        self.btn_open = QtWidgets.QPushButton("Open *.16…"); self.btn_open.clicked.connect(self.open16)

        self.preview = QtWidgets.QLabel("Preview")
        self.preview.setFrameShape(QtWidgets.QFrame.Box)
        self.preview.setAlignment(QtCore.Qt.AlignCenter)
        self.preview.setMinimumSize(512,256)
        self.preview.setScaledContents(True)

        self.btn_export_sheet = QtWidgets.QPushButton("Export spritesheet (TILES.16)…")
        self.btn_export_sheet.clicked.connect(self.export_sheet); self.btn_export_sheet.setEnabled(False)
        self.btn_import_sheet = QtWidgets.QPushButton("Import spritesheet → TILES.16…")
        self.btn_import_sheet.clicked.connect(self.import_sheet); self.btn_import_sheet.setEnabled(False)

        self.btn_export_folder = QtWidgets.QPushButton("Export folder of PNGs…")
        self.btn_export_folder.clicked.connect(self.export_folder); self.btn_export_folder.setEnabled(False)
        self.btn_import_folder = QtWidgets.QPushButton("Import folder of PNGs…")
        self.btn_import_folder.clicked.connect(self.import_folder); self.btn_import_folder.setEnabled(False)

        self.btn_save = QtWidgets.QPushButton("Save *.16…")
        self.btn_save.clicked.connect(self.save16); self.btn_save.setEnabled(False)

        g = QtWidgets.QGridLayout(self)
        g.addWidget(QtWidgets.QLabel("<b>File:</b>"), 0, 0)
        g.addWidget(self.ed_path, 0, 1, 1, 3)
        g.addWidget(self.btn_open, 0, 4)
        g.addWidget(self.preview, 1, 0, 1, 5)
        g.addWidget(self.btn_export_sheet, 2, 0, 1, 2)
        g.addWidget(self.btn_import_sheet, 2, 2, 1, 2)
        g.addWidget(self.btn_export_folder, 3, 0, 1, 2)
        g.addWidget(self.btn_import_folder, 3, 2, 1, 2)
        g.addWidget(self.btn_save, 4, 3, 1, 2)

        self.f: Optional[U5File] = None

    # -------- actions --------
    def open16(self):
        fn, _ = QtWidgets.QFileDialog.getOpenFileName(self, "Open *.16", "", "U5 *.16 (*.16)")
        if not fn: return
        try:
            self.f = read_any16(Path(fn))
        except Exception as e:
            QtWidgets.QMessageBox.critical(self, "Open error", str(e)); return
        self.ed_path.setText(fn)
        self.update_preview()
        tiles = (self.f.kind == "tiles")
        self.btn_export_sheet.setEnabled(tiles)
        self.btn_import_sheet.setEnabled(tiles)
        self.btn_export_folder.setEnabled(True)
        self.btn_import_folder.setEnabled(True)
        self.btn_save.setEnabled(True)

    def save16(self):
        if not self.f: return
        out, _ = QtWidgets.QFileDialog.getSaveFileName(self, "Save *.16", self.ed_path.text() or "output.16", "U5 *.16 (*.16)")
        if not out: return
        try:
            if self.f.kind == "tiles":
                blob = build_tiles_blob(self.f)
            else:
                blob = build_multi_blob(self.f)
            comp = lzw_encode_earlychange(blob)  # matches PC files (incl. DNG3.16, STORY3.16)
            data = struct.pack("<I", len(blob)) + comp
            Path(out).write_bytes(data)
            QtWidgets.QMessageBox.information(self, "Saved", f"Wrote {Path(out).name}")
        except Exception as e:
            QtWidgets.QMessageBox.critical(self, "Save error", str(e))

    def export_sheet(self):
        if not self.f or self.f.kind != "tiles": return
        out, _ = QtWidgets.QFileDialog.getSaveFileName(self, "Save spritesheet PNG", "tiles16_spritesheet.png", "PNG Images (*.png)")
        if not out: return
        sheet = tiles_to_sheet(self.f)
        if not sheet.save(out, "PNG"):
            QtWidgets.QMessageBox.critical(self, "Export error", "Failed to save PNG.")

    def import_sheet(self):
        if not self.f or self.f.kind != "tiles": return
        fn, _ = QtWidgets.QFileDialog.getOpenFileName(self, "Open spritesheet PNG", "", "PNG Images (*.png)")
        if not fn: return
        sheet = QtGui.QImage(fn)
        if sheet.isNull():
            QtWidgets.QMessageBox.critical(self, "Import error", "Failed to load PNG."); return
        try:
            self.f.slots = sheet_to_tiles(sheet)
        except Exception as e:
            QtWidgets.QMessageBox.critical(self, "Import error", str(e)); return
        self.update_preview()

    def export_folder(self):
        if not self.f: return
        out = QtWidgets.QFileDialog.getExistingDirectory(self, "Export to folder")
        if not out: return
        try:
            d = Path(out)
            d.mkdir(parents=True, exist_ok=True)
            if self.f.kind == "tiles":
                for i, s in enumerate(self.f.slots):
                    s.qimage.save(str(d / f"img_{i:03d}.png"), "PNG")
            else:
                export_folder_multi(self.f, d)
        except Exception as e:
            QtWidgets.QMessageBox.critical(self, "Export error", str(e))

    def import_folder(self):
        if not self.f: return
        src = QtWidgets.QFileDialog.getExistingDirectory(self, "Import from folder")
        if not src: return
        try:
            if self.f.kind == "tiles":
                new_slots: List[ImageSlot] = []
                for i in range(TILES_COUNT):
                    p = Path(src) / f"img_{i:03d}.png"
                    if not p.exists():
                        new_slots.append(self.f.slots[i]); continue
                    q = QtGui.QImage(str(p)).convertToFormat(QtGui.QImage.Format.Format_ARGB32)
                    if q.isNull() or q.width()!=TILE_W or q.height()!=TILE_H:
                        raise ValueError(f"{p.name}: expected 16×16 PNG")
                    for y in range(TILE_H):
                        for x in range(TILE_W):
                            k = (q.pixelColor(x,y).red(), q.pixelColor(x,y).green(), q.pixelColor(x,y).blue())
                            if k not in PAL_IDX: raise ValueError(f"{p.name}: Non-EGA color at ({x},{y})")
                    new_slots.append(ImageSlot(TILE_W,TILE_H,q))
                self.f.slots = new_slots
            else:
                self.f.slots = import_folder_multi(self.f, Path(src))
        except Exception as e:
            QtWidgets.QMessageBox.critical(self, "Import error", str(e)); return
        self.update_preview()

    def update_preview(self):
        if not self.f:
            self.preview.setText("Preview"); return
        if self.f.kind == "tiles":
            sheet = tiles_to_sheet(self.f)
        else:
            # simple contact sheet
            imgs = [s.qimage for s in self.f.slots if s is not None]
            if not imgs:
                self.preview.setText("No images in file"); return
            cols = 8; cell = 32
            thumbs = []
            for im in imgs:
                z = max(1, min(cell // max(im.width(), im.height()), 4))
                th = im.scaled(im.width()*z, im.height()*z, QtCore.Qt.KeepAspectRatio, QtCore.Qt.FastTransformation)
                thumbs.append(th)
            rows = math.ceil(len(thumbs)/cols)
            sheet = QtGui.QImage(cols*cell, rows*cell, QtGui.QImage.Format.Format_ARGB32)
            sheet.fill(QtGui.QColor(0,0,0,0))
            p = QtGui.QPainter(sheet)
            i = 0
            for r in range(rows):
                for c in range(cols):
                    if i >= len(thumbs): break
                    th = thumbs[i]
                    x = c*cell + (cell - th.width())//2
                    y = r*cell + (cell - th.height())//2
                    p.drawImage(QtCore.QPoint(x,y), th)
                    i += 1
            p.end()
        self.preview.setPixmap(QtGui.QPixmap.fromImage(sheet))

# ---------------------------------- main --------------------------------------

def main():
    app = QtWidgets.QApplication(sys.argv)
    w = Tool(); w.show()
    sys.exit(app.exec())

if __name__ == "__main__":
    main()

