#!/usr/bin/env python3
# u5_any16_tool.py — Ultima V PC *.16 importer/exporter (exact DOS format)
# Requires: PySide6
#
# • On-disk format: <u32 uncompressed_len_bytes> + LZW (LSB-first)
# • Compression matches src/common/lzw.c, including dictionary-width transitions
# • Supports TILES.16 and all multi-image *.16 (TEXT.16, DNG*.16, STORY*.16, ITEMS.16, MON*.16…)

from __future__ import annotations
from u5_formats import lzw_decode, lzw_encode, read_tiles_blob
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
    return ((w + 7) // 8) * h

# LZW wrappers retain the utility API; the format is shared with the map tools.
def lzw_decode_flexible(header_len_bytes: int, comp: bytes) -> bytes:
    return lzw_decode(comp, header_len_bytes)

def lzw_encode_earlychange(raw: bytes) -> bytes:
    return lzw_encode(raw)

@dataclass
class ImageSlot:
    width: int
    height: int
    qimage: QtGui.QImage        # ARGB32; alpha represents the separate one-bit DOS mask
    mask: Optional[bytes] = None

@dataclass
class U5File:
    path: Optional[Path]
    kind: str                    # "tiles" or "multi"
    count: int = 0
    offsets_16bit: bool = True  # Compatibility field; all containers have paired uint16 offsets
    slots: List[Optional[ImageSlot]] = None
    original_blob: Optional[bytes] = None
    original_signature: Optional[tuple] = None

# =============================== Decode files =================================

def read_any16(path: Path) -> U5File:
    data = path.read_bytes()
    if len(data) < 4:
        raise ValueError("File too small")
    if path.name.upper() == "TILES.16":
        blob = read_tiles_blob(path)
    else:
        uncompressed = struct.unpack("<I", data[:4])[0]
        blob = lzw_decode_flexible(uncompressed, data[4:])

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

    # IMAGE_GetImageView reads two uint16 offsets per image: color and mask.
    if len(blob) < 2:
        raise ValueError("Corrupt container")
    count = struct.unpack_from("<H", blob)[0]
    header_size = 2 + count * 4
    if not count or header_size > len(blob):
        raise ValueError("Truncated image table")
    slots = []
    def dimensions(offset):
        if offset < header_size or offset + 4 > len(blob):
            raise ValueError("Image offset outside resource")
        w, h = struct.unpack_from("<HH", blob, offset)
        if not w or not h or w*h > 8_388_608:
            raise ValueError("Invalid image dimensions")
        return w, h
    for i in range(count):
        color_off, mask_off = struct.unpack_from("<HH", blob, 2+i*4)
        if color_off == 0:
            if mask_off:
                raise ValueError("Mask without color image")
            slots.append(None)
            continue
        w, h = dimensions(color_off)
        stride = bytes_per_row_16(w)
        if color_off+4+stride*h > len(blob):
            raise ValueError("Truncated color image")
        mask = None
        if mask_off:
            mw, mh = dimensions(mask_off)
            if (mw, mh) != (w, h):
                raise ValueError("Color/mask dimensions differ")
            length = mask_len(w, h)
            if mask_off+4+length > len(blob):
                raise ValueError("Truncated image mask")
            mask = blob[mask_off+4:mask_off+4+length]
        img = QtGui.QImage(w, h, QtGui.QImage.Format.Format_ARGB32)
        for y in range(h):
            for x in range(w):
                byte = blob[color_off+4+y*stride+x//2]
                value = (byte >> 4) if x % 2 == 0 else (byte & 15)
                c = QtGui.QColor(PAL_Q[value])
                if mask and mask[y*((w+7)//8)+x//8] & (128 >> (x%8)):
                    c.setAlpha(0)
                img.setPixelColor(x, y, c)
        slots.append(ImageSlot(w, h, img, mask))
    result = U5File(path, "multi", count=count, offsets_16bit=True, slots=slots,
                    original_blob=blob)
    result.original_signature = image_signature(result)
    return result


def image_signature(f):
    return (f.count, tuple(None if s is None else
            (s.width, s.height, s.qimage.width(), s.qimage.height(),
             bytes(s.qimage.convertToFormat(QtGui.QImage.Format.Format_ARGB32).constBits()))
            for s in f.slots))

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
                if c0.alpha() != 255 or c1.alpha() != 255:
                    raise ValueError("TILES.16 cannot store PNG transparency")
                if k0 not in PAL_IDX or k1 not in PAL_IDX:
                    raise ValueError(f"Non-EGA color at ({x},{y})")
                out[off] = ((PAL_IDX[k0] & 0xF) << 4) | (PAL_IDX[k1] & 0xF)
                off += 1
    return bytes(out)

def build_multi_blob(f: U5File) -> bytes:
    if f.kind != "multi" or len(f.slots) != f.count or not 0 < f.count <= 16383:
        raise ValueError("Invalid image count")
    if f.original_blob is not None and image_signature(f) == f.original_signature:
        return f.original_blob
    header = bytearray(2+4*f.count)
    struct.pack_into("<H", header, 0, f.count)
    body = bytearray()
    def offset():
        value = len(header)+len(body)
        if value > 65535:
            raise ValueError("Image offsets exceed DOS 16-bit limit")
        return value
    for i, slot in enumerate(f.slots):
        if slot is None:
            continue
        w, h, img = slot.width, slot.height, slot.qimage
        if not 0 < w <= 65535 or not 0 < h <= 65535 or (w, h) != (img.width(), img.height()):
            raise ValueError("Invalid image dimensions")
        color_off = offset()
        body.extend(struct.pack("<HH", w, h))
        pixels = bytearray(bytes_per_row_16(w)*h)
        mask = bytearray(mask_len(w, h))
        has_mask = slot.mask is not None
        for y in range(h):
            for x in range(w):
                c = img.pixelColor(x, y)
                if c.alpha() not in (0, 255):
                    raise ValueError("DOS masks support only fully opaque or transparent pixels")
                rgb = (c.red(), c.green(), c.blue())
                if c.alpha() == 0:
                    has_mask = True
                    mask[y*((w+7)//8)+x//8] |= 128 >> (x%8)
                    value = PAL_IDX.get(rgb, 0)
                elif rgb in PAL_IDX:
                    value = PAL_IDX[rgb]
                else:
                    raise ValueError(f"Non-EGA color at ({x},{y})")
                pixels[y*bytes_per_row_16(w)+x//2] |= value << (4 if x%2 == 0 else 0)
        body.extend(pixels)
        mask_off = 0
        if has_mask:
            mask_off = offset()
            body.extend(struct.pack("<HH", w, h))
            body.extend(mask)
        struct.pack_into("<HH", header, 2+i*4, color_off, mask_off)
    return bytes(header+body)

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

