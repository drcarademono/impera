#!/usr/bin/env python3
# tiles16_tool.py — Ultima V TILES.16 <-> PNG spritesheet
# Writes DOS format: 65536-byte length header and engine-compatible LSB-first LZW.

from __future__ import annotations
from u5_formats import lzw_decode, lzw_encode, read_tiles_blob
import sys, struct
from pathlib import Path
from typing import List, Tuple, Optional
from PySide6 import QtCore, QtGui, QtWidgets

# ---------- Palette & constants ----------
EGA16: List[Tuple[int,int,int]] = [
    (0,0,0),(0,0,170),(0,170,0),(0,170,170),
    (170,0,0),(170,0,170),(170,85,0),(170,170,170),
    (85,85,85),(85,85,255),(85,255,85),(85,255,255),
    (255,85,85),(255,85,255),(255,255,85),(255,255,255),
]
PAL_Q = [QtGui.QColor(r,g,b) for (r,g,b) in EGA16]
PAL_IDX = { (r,g,b): i for i,(r,g,b) in enumerate(EGA16) }

TILES, TILE_W, TILE_H = 512, 16, 16
SHEET_COLS, SHEET_ROWS = 32, 16
RAW_SIZE = 512 * 16 * 8  # 65536

# LZW wrappers use the shared engine-compatible codec.
def lzw_decode_gif_latechange(comp: bytes, expected_len: int) -> bytes:
    return lzw_decode(comp, expected_len)

def lzw_encode_gif_latechange(raw: bytes) -> bytes:
    return lzw_encode(raw)

def read_tiles16_blob(path: Path) -> bytes:
    return read_tiles_blob(path)

def blob_to_images(blob: bytes) -> List[QtGui.QImage]:
    if len(blob) != RAW_SIZE:
        raise ValueError("Tiles blob must contain exactly 65536 bytes")
    imgs: List[QtGui.QImage] = []
    off = 0
    for _ in range(TILES):
        img = QtGui.QImage(TILE_W, TILE_H, QtGui.QImage.Format.Format_RGB888)
        for y in range(TILE_H):
            row = blob[off:off+8]; off += 8
            x = 0
            for b in row:
                hi = (b >> 4) & 0x0F  # first pixel = high nibble
                lo = b & 0x0F        # second = low
                img.setPixelColor(x, y, PAL_Q[hi]); x += 1
                img.setPixelColor(x, y, PAL_Q[lo]); x += 1
        imgs.append(img)
    return imgs

def images_to_blob(imgs: List[QtGui.QImage]) -> bytes:
    if len(imgs) != TILES:
        raise ValueError(f"Need {TILES} tiles; got {len(imgs)}")
    out = bytearray(RAW_SIZE)
    off = 0
    for img in imgs:
        if img.width() != TILE_W or img.height() != TILE_H:
            raise ValueError("Each tile must be exactly 16x16 px.")
        for y in range(TILE_H):
            for x in range(0, TILE_W, 2):
                c0 = img.pixelColor(x, y)
                c1 = img.pixelColor(x+1, y)
                if c0.alpha() != 255 or c1.alpha() != 255:
                    raise ValueError("TILES.16 cannot store PNG transparency")
                k0 = (c0.red(), c0.green(), c0.blue())
                k1 = (c1.red(), c1.green(), c1.blue())
                if k0 not in PAL_IDX or k1 not in PAL_IDX:
                    raise ValueError(f"Non-EGA color at ({x},{y}). Use the 16-color EGA palette.")
                out[off] = ((PAL_IDX[k0] & 0xF) << 4) | (PAL_IDX[k1] & 0xF)
                off += 1
    return bytes(out)

# ---------- Spritesheet helpers ----------
def images_to_sheet(imgs: List[QtGui.QImage]) -> QtGui.QImage:
    sheet = QtGui.QImage(SHEET_COLS*TILE_W, SHEET_ROWS*TILE_H, QtGui.QImage.Format.Format_RGB888)
    sheet.fill(QtGui.QColor(0,0,0))
    p = QtGui.QPainter(sheet)
    for i, img in enumerate(imgs):
        r, c = divmod(i, SHEET_COLS)
        p.drawImage(QtCore.QPoint(c*TILE_W, r*TILE_H), img)
    p.end()
    return sheet

def sheet_to_images(sheet: QtGui.QImage) -> List[QtGui.QImage]:
    if sheet.width() != SHEET_COLS*TILE_W or sheet.height() != SHEET_ROWS*TILE_H:
        raise ValueError(f"Spritesheet must be {SHEET_COLS*TILE_W}x{SHEET_ROWS*TILE_H} px")
    tiles: List[QtGui.QImage] = []
    for r in range(SHEET_ROWS):
        for c in range(SHEET_COLS):
            tiles.append(sheet.copy(QtCore.QRect(c*TILE_W, r*TILE_H, TILE_W, TILE_H)))
    return tiles

# ---------- GUI ----------
class Tool(QtWidgets.QWidget):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("Ultima V TILES.16 ⇄ PNG (Exact DOS format)")
        self.resize(860, 600)

        self.ed_tiles = QtWidgets.QLineEdit()
        self.ed_png   = QtWidgets.QLineEdit()
        bt_tiles = QtWidgets.QPushButton("Browse…"); bt_tiles.clicked.connect(self.pick_tiles)
        bt_png   = QtWidgets.QPushButton("Browse…"); bt_png.clicked.connect(self.pick_png)

        self.btn_export = QtWidgets.QPushButton("Export TILES.16 → PNG")
        self.btn_export.clicked.connect(self.do_export)
        self.btn_import = QtWidgets.QPushButton("Import PNG → TILES.16")
        self.btn_import.clicked.connect(self.do_import)

        self.preview = QtWidgets.QLabel("Preview")
        self.preview.setFrameShape(QtWidgets.QFrame.Box)
        self.preview.setAlignment(QtCore.Qt.AlignCenter)
        self.preview.setMinimumSize(512, 256)
        self.preview.setScaledContents(True)

        g = QtWidgets.QGridLayout()
        g.addWidget(QtWidgets.QLabel("<b>TILES.16:</b>"), 0, 0)
        g.addWidget(self.ed_tiles, 0, 1, 1, 3)
        g.addWidget(bt_tiles, 0, 4)
        g.addWidget(QtWidgets.QLabel("<b>PNG spritesheet:</b>"), 1, 0)
        g.addWidget(self.ed_png, 1, 1, 1, 3)
        g.addWidget(bt_png, 1, 4)
        g.addWidget(self.btn_export, 2, 1, 1, 2)
        g.addWidget(self.btn_import, 2, 3, 1, 2)
        g.addWidget(self.preview, 3, 0, 1, 5)

        v = QtWidgets.QVBoxLayout(self); v.addLayout(g)
        self.setAcceptDrops(True)

    # pickers
    def pick_tiles(self):
        fn, _ = QtWidgets.QFileDialog.getOpenFileName(self, "Select TILES.16", "", "TILES.16 (*);;All files (*)")
        if fn:
            self.ed_tiles.setText(fn)
            self.update_preview_tiles(Path(fn))
    def pick_png(self):
        fn, _ = QtWidgets.QFileDialog.getOpenFileName(self, "Select PNG spritesheet", "", "PNG Images (*.png)")
        if fn:
            self.ed_png.setText(fn)
            self.update_preview_png(Path(fn))

    # actions
    def do_export(self):
        try:
            p = Path(self.ed_tiles.text())
            blob = read_tiles16_blob(p)
            sheet = images_to_sheet(blob_to_images(blob))
            out, _ = QtWidgets.QFileDialog.getSaveFileName(self, "Save PNG", "tiles16_spritesheet.png", "PNG Images (*.png)")
            if not out: return
            if not sheet.save(out, "PNG"):
                raise RuntimeError("Failed to save PNG")
            self.ed_png.setText(out)
            self.preview.setPixmap(QtGui.QPixmap.fromImage(sheet))
        except Exception as e:
            QtWidgets.QMessageBox.critical(self, "Export error", str(e))

    def do_import(self):
        try:
            p = Path(self.ed_png.text())
            sheet = QtGui.QImage(str(p))
            if sheet.isNull():
                raise RuntimeError("Failed to load PNG")
            imgs = sheet_to_images(sheet)
            blob = images_to_blob(imgs)

            # Encode EXACT DOS format
            comp = lzw_encode_gif_latechange(blob)
            data = struct.pack("<I", RAW_SIZE) + comp  # header = 65536 bytes

            out, _ = QtWidgets.QFileDialog.getSaveFileName(self, "Save TILES.16", "TILES.16", "All files (*)")
            if not out: return
            Path(out).write_bytes(data)
            QtWidgets.QMessageBox.information(self, "Done", "Wrote DOS-format TILES.16 (header 65536 + LZW).")
            self.ed_tiles.setText(out)
        except Exception as e:
            QtWidgets.QMessageBox.critical(self, "Import error", str(e))

    # preview helpers
    def update_preview_tiles(self, path: Path):
        try:
            blob = read_tiles16_blob(path)
            sheet = images_to_sheet(blob_to_images(blob))
            self.preview.setPixmap(QtGui.QPixmap.fromImage(sheet))
        except Exception as e:
            self.preview.setText(f"Preview error:\n{e}")
    def update_preview_png(self, path: Path):
        img = QtGui.QImage(str(path))
        if img.isNull():
            self.preview.setText("Invalid PNG")
        else:
            self.preview.setPixmap(QtGui.QPixmap.fromImage(img))

    # drag & drop
    def dragEnterEvent(self, e: QtGui.QDragEnterEvent):
        if e.mimeData().hasUrls(): e.acceptProposedAction()
    def dropEvent(self, e: QtGui.QDropEvent):
        urls = e.mimeData().urls()
        if not urls: return
        p = Path(urls[0].toLocalFile())
        if p.suffix.lower()==".png":
            self.ed_png.setText(str(p)); self.update_preview_png(p)
        else:
            self.ed_tiles.setText(str(p)); self.update_preview_tiles(p)

def main():
    app = QtWidgets.QApplication(sys.argv)
    w = Tool(); w.show()
    sys.exit(app.exec())

if __name__ == "__main__":
    main()

