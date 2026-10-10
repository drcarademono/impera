#!/usr/bin/env python3
import sys
from pathlib import Path

from PySide6 import QtWidgets, QtCore

# ---------- Unicode → ASCII map ----------

UNICODE_MAP = {
    "—": "-", "–": "-",
    "“": '"', "”": '"',
    "‘": "'", "’": "'",
    "…": "...",
    "\u00A0": " ",   # non-breaking space
}


def normalize_to_ascii(text: str) -> str:
    """
    Replace common Unicode punctuation with ASCII equivalents.
    Anything else >127 becomes '?' so the file stays byte-safe.
    """
    out = []
    for ch in text:
        code = ord(ch)
        if ch in UNICODE_MAP:
            out.append(UNICODE_MAP[ch])
        elif code <= 127:
            out.append(ch)
        else:
            out.append("?")
    return "".join(out)


# ---------- Data structures ----------

class Segment:
    def __init__(self, index: int, start_offset: int, end_offset: int, text: str):
        self.index = index
        self.start = start_offset          # inclusive
        self.end = end_offset              # exclusive (start of next segment/window)
        self.text = text                   # decoded ASCII text (no NUL)

        # These will be set by the loader
        self.max_payload = 0               # in bytes, hard cap for this segment
        self.orig_payload_len = 0          # bytes of original text before its NUL


# ---------- Parsing STORY.DAT ----------

def load_story_dat(path: Path):
    """
    Load STORY.DAT and return (raw_bytes, [Segment,...]).

    We treat the file as a sequence of "windows":
      - Segment 0: [0, start_of_segment1)
      - Segment 1: [start_of_segment1, start_of_segment2)
      - etc.

    Segment starts are inferred from NUL positions:
      first start at 0, then each NUL+1 is a potential start.
    """
    data = path.read_bytes()
    n = len(data)

    # All NUL positions
    nul_positions = [i for i, b in enumerate(data) if b == 0x00]
    if not nul_positions:
        raise ValueError("No NUL bytes found; not a valid Ultima V STORY.DAT?")

    # Segment starts: 0 and each NUL+1 (if in range)
    starts = [0]
    for pos in nul_positions:
        if pos + 1 < n:
            starts.append(pos + 1)
    starts = sorted(set(starts))

    segments = []

    for idx, start in enumerate(starts):
        if idx + 1 < len(starts):
            end = starts[idx + 1]
        else:
            end = n
        if end <= start:
            continue

        # Window for this segment
        window = data[start:end]

        # Find first NUL inside this window = end of original payload
        try:
            nul_idx = window.index(0x00)
            payload = window[:nul_idx]
        except ValueError:
            # No NUL in window, treat full window as payload
            payload = window
            nul_idx = len(window)

        # Decode payload as ASCII
        try:
            text = payload.decode("ascii", errors="replace")
        except Exception:
            text = "".join(chr(b) if 0 <= b < 128 else "?" for b in payload)

        seg = Segment(
            index=len(segments),
            start_offset=start,
            end_offset=end,
            text=text,
        )
        seg.orig_payload_len = len(payload)
        # HARD limit: you may not write more than the original payload length;
        # the NUL terminator must stay at start + orig_payload_len.
        seg.max_payload = seg.orig_payload_len

        segments.append(seg)

    return data, segments


# ---------- GUI: per-segment editor ----------

class SegmentEditor(QtWidgets.QWidget):
    """
    A widget with:
      - Label: "Segment X: used / max bytes"
      - QPlainTextEdit enforcing max bytes (after ASCII normalization)
    """
    textChangedForSave = QtCore.Signal(int, str)  # segment_index, text

    def __init__(self, segment: Segment, parent=None):
        super().__init__(parent)
        self.segment = segment
        self._updating = False
        self._last_good_text = segment.text

        layout = QtWidgets.QVBoxLayout(self)

        self.label = QtWidgets.QLabel(self)
        self.edit = QtWidgets.QPlainTextEdit(self)

        # Monospace font for easier visual alignment
        font = self.edit.font()
        font.setFamily("monospace")
        self.edit.setFont(font)

        layout.addWidget(self.label)
        layout.addWidget(self.edit)

        self.edit.setPlainText(segment.text)
        self.edit.textChanged.connect(self.on_text_changed)

        self.update_label()

    def current_payload_bytes(self, text: str) -> int:
        norm = normalize_to_ascii(text)
        return len(norm.encode("ascii", errors="replace"))

    def update_label(self):
        used = self.current_payload_bytes(self.edit.toPlainText())
        maxb = self.segment.max_payload
        self.label.setText(
            f"Segment {self.segment.index + 1}: {used} / {maxb} bytes"
        )

    def on_text_changed(self):
        if self._updating:
            return

        self._updating = True
        try:
            text = self.edit.toPlainText()
            # Normalize to ASCII
            norm = normalize_to_ascii(text)
            used = len(norm.encode("ascii", errors="replace"))
            maxb = self.segment.max_payload

            if used <= maxb:
                # Accept change, ensure the text box holds the normalized text
                if norm != text:
                    cursor = self.edit.textCursor()
                    pos = cursor.position()
                    self.edit.blockSignals(True)
                    self.edit.setPlainText(norm)
                    cursor = self.edit.textCursor()
                    cursor.setPosition(min(pos, len(norm)))
                    self.edit.setTextCursor(cursor)
                    self.edit.blockSignals(False)

                self._last_good_text = self.edit.toPlainText()
                self.update_label()
                self.textChangedForSave.emit(
                    self.segment.index,
                    self._last_good_text,
                )
            else:
                # Revert to last good text
                self.edit.blockSignals(True)
                self.edit.setPlainText(self._last_good_text)
                cursor = self.edit.textCursor()
                cursor.movePosition(cursor.End)
                self.edit.setTextCursor(cursor)
                self.edit.blockSignals(False)
                # label unchanged
        finally:
            self._updating = False


# ---------- Main window ----------

class StoryEditorWindow(QtWidgets.QMainWindow):
    def __init__(self, dat_path: Path, parent=None):
        super().__init__(parent)
        self.setWindowTitle(f"Ultima V STORY.DAT Editor - {dat_path.name}")

        self.dat_path = dat_path
        self.original_bytes, self.segments = load_story_dat(dat_path)
        self.segment_texts = {seg.index: seg.text for seg in self.segments}

        central = QtWidgets.QWidget(self)
        self.setCentralWidget(central)
        vbox = QtWidgets.QVBoxLayout(central)

        scroll = QtWidgets.QScrollArea(self)
        scroll.setWidgetResizable(True)
        vbox.addWidget(scroll)

        inner = QtWidgets.QWidget()
        scroll.setWidget(inner)
        inner_layout = QtWidgets.QVBoxLayout(inner)

        # Create a SegmentEditor for each segment
        for seg in self.segments:
            editor = SegmentEditor(seg, self)
            editor.textChangedForSave.connect(self.on_segment_text_changed)
            inner_layout.addWidget(editor)

        inner_layout.addStretch(1)

        # Buttons
        btn_box = QtWidgets.QHBoxLayout()
        self.save_btn = QtWidgets.QPushButton("Save As...", self)
        self.save_btn.clicked.connect(self.save_as)
        btn_box.addStretch(1)
        btn_box.addWidget(self.save_btn)
        vbox.addLayout(btn_box)

        self.resize(900, 700)

    def on_segment_text_changed(self, index: int, text: str):
        self.segment_texts[index] = text

    def save_as(self):
        out_path_str, _ = QtWidgets.QFileDialog.getSaveFileName(
            self,
            "Save STORY.DAT As",
            str(self.dat_path.with_suffix(".new.DAT")),
            "DAT files (*.DAT);;All files (*)",
        )
        if not out_path_str:
            return

        out_path = Path(out_path_str)

        try:
            self._write_story_dat(out_path)
        except Exception as e:
            QtWidgets.QMessageBox.critical(
                self, "Error Saving", f"Failed to save file:\n{e}"
            )
            return

        QtWidgets.QMessageBox.information(
            self, "Saved", f"Saved updated STORY.DAT to:\n{out_path}"
        )

    def _write_story_dat(self, out_path: Path):
        """
        Write updated STORY.DAT:

        - Start from original bytes.
        - For each segment, overwrite only:
            * the text payload starting at seg.start
            * pad up to the original terminator with spaces
            * ensure the NUL terminator is still at start + orig_payload_len
        - Leave everything after the original terminator exactly as in the original.
        """
        out = bytearray(self.original_bytes)

        for seg in self.segments:
            text = self.segment_texts.get(seg.index, seg.text)
            norm = normalize_to_ascii(text)
            payload = norm.encode("ascii", errors="replace")

            maxb = seg.max_payload  # == seg.orig_payload_len
            if len(payload) > maxb:
                raise ValueError(
                    f"Segment {seg.index + 1} too long: {len(payload)} bytes "
                    f"(max {maxb})"
                )

            # Original terminator position MUST remain the break:
            old_term_pos = seg.start + seg.orig_payload_len
            if old_term_pos >= seg.end:
                # Safety check; should not happen in a normal STORY.DAT
                raise ValueError(
                    f"Segment {seg.index + 1}: original terminator outside window."
                )

            # 1) Write new payload at segment start
            out[seg.start : seg.start + len(payload)] = payload

            # 2) Fill remaining payload region up to the original terminator with spaces
            fill_start = seg.start + len(payload)
            if fill_start < old_term_pos:
                out[fill_start:old_term_pos] = b" " * (old_term_pos - fill_start)

            # 3) Ensure NUL terminator at the original terminator location
            out[old_term_pos] = 0x00

            # 4) Do NOT touch bytes after old_term_pos; they stay exactly as in original

        out_path.write_bytes(out)


# ---------- Entry point ----------

def main():
    app = QtWidgets.QApplication(sys.argv)

    if len(sys.argv) < 2:
        QtWidgets.QMessageBox.critical(
            None,
            "Usage",
            "Run as:\n\nu5_story_gui.py /path/to/STORY.DAT",
        )
        return

    dat_path = Path(sys.argv[1])
    if not dat_path.is_file():
        QtWidgets.QMessageBox.critical(
            None,
            "Error",
            f"File not found:\n{dat_path}",
        )
        return

        # note: never reached
    win = StoryEditorWindow(dat_path)
    win.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()

