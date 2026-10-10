#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Ultima V INIT.GAM Editor (GUI) — safe startup patch
- Handles launching with no file argument (allocates default buffer)
- u8/u16 guard against out-of-range; setters auto-extend

Dependencies: PySide6
Run: python u5_init_editor.py [INIT.GAM]
"""

from __future__ import annotations
import sys, struct, textwrap
from dataclasses import dataclass
from typing import List, Tuple, Dict, Optional
from pathlib import Path

from PySide6 import QtCore, QtGui, QtWidgets

LE = "<"  # little-endian for struct
DEFAULT_SIZE = 0x1060  # typical INIT/SAVED.GAM length

# --------------------------- Known Layout ---------------------------

LOCATION_NAMES = {
    0x00: "Britannia/Underworld (overworld slot)",
    0x01: "Moonglow", 0x02: "Britain", 0x03: "Jhelom", 0x04: "Yew",
    0x05: "Minoc", 0x06: "Trinsic", 0x07: "Skara Brae", 0x08: "New Magincia",
    0x09: "Fogsbane", 0x0A: "Stormcrow", 0x0B: "Greyhaven", 0x0C: "Waveguide",
    0x0D: "Iolo's hut", 0x0E: "Sutek's hut", 0x0F: "Sin'Vraal's hut",
    0x10: "Grendel's hut",
    0x11: "Castle British", 0x12: "Castle Blackthorn",
    0x13: "West Britanny", 0x14: "North Britanny", 0x15: "East Britanny",
    0x16: "Paws", 0x17: "Cove", 0x18: "Buccaneer's Den",
    0x19: "Ararat", 0x1A: "Bordermarch", 0x1B: "Farthing", 0x1C: "Windemere",
    0x1D: "Stonegate", 0x1E: "Lycaeum", 0x1F: "Empath Abbey", 0x20: "Serpent's Hold",
    0x21: "Deceit", 0x22: "Despise", 0x23: "Destard", 0x24: "Wrong",
    0x25: "Covetous", 0x26: "Shame", 0x27: "Hythloth", 0x28: "Doom",
    0xFF: "Combat/Resting/Shrine"
}

def clamp(v, lo, hi): return max(lo, min(hi, v))

@dataclass
class Field:
    name: str; offset: int; length: int; fmt: str; tooltip: str
    min_val: Optional[int] = None; max_val: Optional[int] = None; is_signed: bool = False

FIELDS: Dict[str, Field] = {
    "party_location": Field("Party Location", 0x2ED, 1, "B",
        "One-byte index of the current map. 0x00=Overworld, 0x0D=Iolo's hut." ,0,255),
    "party_z": Field("Z (Level/Floor/World)", 0x2EF, 1, "B",
        "Overworld: 0x00=Britannia, 0xFF=Underworld. Towns: FF=Basement, 00=Ground, 01=1st… Dungeons: 00..07 levels.",0,255),
    "party_x": Field("X", 0x2F0, 1, "B", "X tile (0-255 overworld; 0-31 inside maps).",0,255),
    "party_y": Field("Y", 0x2F1, 1, "B", "Y tile (0-255 overworld; 0-31 inside maps).",0,255),
    "transport_mode": Field("Transport Mode", 0x2D6, 1, "B", "Encoded travel mode: foot/ship/skiff/horse/carpet.",0,255),
    "active_char": Field("Active Character Index", 0x2D5, 1, "B", "Current party member index (0–5) or 0xFF none.",0,255),
    "month": Field("Month", 0x2D7, 1, "B", "1–13",1,13),
    "day": Field("Day", 0x2D8, 1, "B", "1–28",1,28),
    "hour": Field("Hour", 0x2D9, 1, "B", "0–23",0,23),
    "minute": Field("Minute", 0x2DB, 1, "B", "0–59",0,59),
    "karma": Field("Karma", 0x2E2, 1, "B", "0–255 virtue standing.",0,255),
    "food": Field("Food", 0x202, 2, "H", "0–9999",0,9999),
    "gold": Field("Gold", 0x204, 2, "H", "0–9999",0,9999),
    "num_party": Field("# Party Members", 0x2B5, 1, "B", "1–6",1,6),
    "trammel": Field("Trammel Phase", 0x2DF, 1, "B", "0x30–0x37; affects moongates 00–04h.",0,255),
    "felucca": Field("Felucca Phase", 0x2E0, 1, "B", "0x30–0x37; affects moongates 20–23h.",0,255),
}

PLOT_FLAGS: Dict[str, Field] = {
    "grapple": Field("Grapple", 0x209, 1, "B", "0 or 0xFF (owned).",0,255),
    "amulet": Field("Amulet of LB", 0x20D, 1, "B", "0 or 0xFF (owned).",0,255),
    "crown": Field("Crown of LB", 0x20E, 1, "B", "0 or 0xFF (owned).",0,255),
    "sceptre": Field("Sceptre of LB", 0x20F, 1, "B", "0 or 0xFF (owned).",0,255),
    "shard_false": Field("Shard of Falsehood", 0x210, 1, "B", "0 or 0xFF (owned).",0,255),
    "shard_hatred": Field("Shard of Hatred", 0x211, 1, "B", "0 or 0xFF (owned).",0,255),
    "shard_coward": Field("Shard of Cowardice", 0x212, 1, "B", "0 or 0xFF (owned).",0,255),
    "watch": Field("Pocket Watch", 0x217, 1, "B", "0 or 0xFF (owned).",0,255),
    "black_badge": Field("Black Badge", 0x218, 1, "B", "0 or 0xFF (owned).",0,255),
    "sandalwood": Field("Sandalwood Box", 0x219, 1, "B", "0 or 0xFF (owned).",0,255),
}

REAGENTS = [
    ("Sulphurous ash", 0x2AA), ("Ginseng", 0x2AB), ("Garlic", 0x2AC), ("Spider silk", 0x2AD),
    ("Blood moss", 0x2AE), ("Black pearl", 0x2AF), ("Nightshade", 0x2B0), ("Mandrake root", 0x2B1),
]

MOONSTONE_BASES = {"x":0x28A, "y":0x292, "flag":0x29A, "z":0x2A2}

SHRINE_FLAGS = {"Ordained (first mantra use)":0x326, "Completed (visited Codex & returned)":0x328}
SHRINE_BITS = ["Honesty","Compassion","Valor","Justice","Sacrifice","Honor","Spirituality","Humility"]

DUNGEON_FLAGS_OFFSET = 0x32A
DUNGEON_NAMES = ["Deceit","Despise","Destard","Wrong","Covetous","Shame","Hythloth","Doom"]
SHRINE_STATUS_OFFSET = 0x332

# --------------------------- Model ---------------------------
class InitGamModel(QtCore.QObject):
    data_changed = QtCore.Signal()
    def __init__(self, path: Optional[Path]=None):
        super().__init__()
        self.path: Optional[Path] = None
        self.bytes = bytearray(DEFAULT_SIZE)
        if path:
            self.load(path)

    def load(self, path: Path):
        self.bytes = bytearray(path.read_bytes())
        self.path = path
        self.data_changed.emit()

    def save(self, path: Optional[Path]=None):
        if path is None:
            if self.path is None:
                raise ValueError("No file path specified")
            path = self.path
        Path(path).write_bytes(self.bytes)

    def _ensure_len(self, size:int):
        if size > len(self.bytes):
            self.bytes.extend(b"\x00" * (size - len(self.bytes)))

    def u8(self, off:int) -> int:
        if 0 <= off < len(self.bytes):
            return self.bytes[off]
        return 0

    def u16(self, off:int) -> int:
        if 0 <= off+1 < len(self.bytes):
            return struct.unpack_from(LE+"H", self.bytes, off)[0]
        return 0

    def set_u8(self, off:int, val:int):
        self._ensure_len(off+1)
        self.bytes[off] = val & 0xFF
        self.data_changed.emit()

    def set_u16(self, off:int, val:int):
        self._ensure_len(off+2)
        struct.pack_into(LE+"H", self.bytes, off, val & 0xFFFF)
        self.data_changed.emit()

# --------------------------- Widgets ---------------------------
class LabeledSpin(QtWidgets.QWidget):
    valueChanged = QtCore.Signal(int)
    def __init__(self, title:str, value:int, lo:int, hi:int, tooltip:str=""):
        super().__init__()
        lay = QtWidgets.QHBoxLayout(self)
        lay.setContentsMargins(0, 0, 0, 0)

        lbl = QtWidgets.QLabel(title)
        lbl.setToolTip(tooltip)

        sp = QtWidgets.QSpinBox()
        sp.setRange(lo, hi)
        sp.setKeyboardTracking(True)  # emit while typing
        sp.setValue(value)
        sp.setToolTip(tooltip)

        lay.addWidget(lbl)
        lay.addWidget(sp, 1)

        self._spin = sp  # expose inner spinbox for focus checks, etc.

        # Relay signals robustly (typing, arrows, and focus-leave)
        sp.valueChanged.connect(lambda v: self.valueChanged.emit(v))
        sp.editingFinished.connect(lambda: self.valueChanged.emit(self._spin.value()))

    def setValue(self, v:int):
        """Class: LabeledSpin"""
        self._spin.setValue(v)

    def value(self) -> int:
        """Class: LabeledSpin"""
        return self._spin.value()

class PartyTab(QtWidgets.QWidget):
    def __init__(self, model: InitGamModel):
        super().__init__()
        self.model = model
        lay = QtWidgets.QFormLayout(self)

        # --- Existing "start position" controls ---
        self.loc_combo = QtWidgets.QComboBox()
        for k in sorted(LOCATION_NAMES.keys(), key=lambda x: (x==0xFF, x)):
            self.loc_combo.addItem(f"{k:02X} – {LOCATION_NAMES[k]}", k)
        self.loc_combo.setToolTip("0x2ED: Map index the party is in.")
        lay.addRow("Party Location (0x2ED)", self.loc_combo)

        self.z_spin = LabeledSpin("Z (0x2EF)", model.u8(FIELDS["party_z"].offset), 0, 255, FIELDS["party_z"].tooltip)
        self.x_spin = LabeledSpin("X (0x2F0)", model.u8(FIELDS["party_x"].offset), 0, 255, FIELDS["party_x"].tooltip)
        self.y_spin = LabeledSpin("Y (0x2F1)", model.u8(FIELDS["party_y"].offset), 0, 255, FIELDS["party_y"].tooltip)
        lay.addRow(self.z_spin); lay.addRow(self.x_spin); lay.addRow(self.y_spin)

        self.transport = LabeledSpin("Transport Mode (0x2D6)", model.u8(FIELDS["transport_mode"].offset), 0, 255, FIELDS["transport_mode"].tooltip)
        self.active_char = LabeledSpin("Active Char (0x2D5)", model.u8(FIELDS["active_char"].offset), 0, 255, FIELDS["active_char"].tooltip)
        self.num_party = LabeledSpin("# Party Members (0x2B5)", model.u8(FIELDS["num_party"].offset), 1, 6, FIELDS["num_party"].tooltip)
        lay.addRow(self.transport); lay.addRow(self.active_char); lay.addRow(self.num_party)

        # --- NEW: Party members table (who’s in party, HP, status) ---
        members_group = QtWidgets.QGroupBox("Party Members (records at 0x0002 + n*32)")
        vbox = QtWidgets.QVBoxLayout(members_group)
        self.members_tbl = QtWidgets.QTableWidget(16, 5)
        self.members_tbl.setHorizontalHeaderLabels(["#", "Name", "In Party", "Status", "HP (cur/max)"])
        self.members_tbl.horizontalHeader().setSectionResizeMode(QtWidgets.QHeaderView.Stretch)
        self._member_inparty = []
        self._member_status = []
        self._member_hp_cur = []
        self._member_hp_max = []

        STATUS_CHOICES = [("Good",'G'), ("Poisoned",'P'), ("Asleep",'S'), ("Charmed",'C'), ("Dead",'D')]
        for i in range(16):
            # row label
            idx_item = QtWidgets.QTableWidgetItem(str(i+1))
            idx_item.setFlags(QtCore.Qt.ItemIsEnabled)
            self.members_tbl.setItem(i, 0, idx_item)

            # name (read-only preview)
            name_item = QtWidgets.QTableWidgetItem("")  # filled in refresh
            name_item.setFlags(QtCore.Qt.ItemIsEnabled)
            self.members_tbl.setItem(i, 1, name_item)

            # in party checkbox
            cb = QtWidgets.QCheckBox()
            self.members_tbl.setCellWidget(i, 2, cb)
            self._member_inparty.append(cb)
            cb.stateChanged.connect(lambda _=None, r=i: (
                self.model.set_u8(0x02 + r*32 + 0x1F, 0x00 if self._member_inparty[r].isChecked() else 0xFF),
                self.model.set_u8(FIELDS["num_party"].offset, sum(1 for k in range(16) if self._member_inparty[k].isChecked()))
            ))

            # status combo
            combo = QtWidgets.QComboBox()
            for label, ch in STATUS_CHOICES:
                combo.addItem(label, ord(ch))
            self.members_tbl.setCellWidget(i, 3, combo)
            self._member_status.append(combo)
            combo.currentIndexChanged.connect(lambda _=None, r=i: (
                self.model.set_u8(0x02 + r*32 + 0x0B, int(self._member_status[r].currentData()))
            ))

            # HP editors
            w = QtWidgets.QWidget(); h = QtWidgets.QHBoxLayout(w); h.setContentsMargins(0,0,0,0)
            sp_cur = QtWidgets.QSpinBox(); sp_cur.setRange(0, 240)
            sp_max = QtWidgets.QSpinBox(); sp_max.setRange(1, 240)
            h.addWidget(sp_cur); h.addWidget(QtWidgets.QLabel("/")); h.addWidget(sp_max)
            self.members_tbl.setCellWidget(i, 4, w)
            self._member_hp_cur.append(sp_cur); self._member_hp_max.append(sp_max)
            sp_cur.valueChanged.connect(lambda _=None, r=i: self.model.set_u16(0x02 + r*32 + 0x10, self._member_hp_cur[r].value()))
            sp_max.valueChanged.connect(lambda _=None, r=i: self.model.set_u16(0x02 + r*32 + 0x12, self._member_hp_max[r].value()))

        vbox.addWidget(self.members_tbl)
        lay.addRow(members_group)

        # Bind signals (ensure spins fire on both arrow and typed changes)
        self.refresh_from_model()
        self.loc_combo.currentIndexChanged.connect(self.on_apply)
        # connect to the LabeledSpin relay AND directly to inner spin for robustness
        self.z_spin.valueChanged.connect(self.on_apply)
        self.x_spin.valueChanged.connect(self.on_apply)
        self.y_spin.valueChanged.connect(self.on_apply)
        self.z_spin._spin.editingFinished.connect(self.on_apply)
        self.x_spin._spin.editingFinished.connect(self.on_apply)
        self.y_spin._spin.editingFinished.connect(self.on_apply)

        self.transport.valueChanged.connect(self.on_apply)
        self.active_char.valueChanged.connect(self.on_apply)
        self.num_party.valueChanged.connect(self.on_apply)
        self.model.data_changed.connect(self.refresh_from_model)

        # Help text (kept as in your current build)
        help_box = QtWidgets.QTextEdit()
        help_box.setReadOnly(True)
        help_box.setStyleSheet("QTextEdit { background: #f7f7f7; }")
        help_box.setText(textwrap.dedent("""\
            These values determine where a *new game* begins (unless the intro overwrites them):
            • 0x2ED map index; • 0x2EF–0x2F1 Z/X/Y.
            Overworld: Z=00 (Britannia) or FF (Underworld), X/Y 0..255.
            Towns: Z=floor (FF basement, 00 ground), X/Y 0..31.
            Dungeons: Z=00..07 (levels 1..8), X/Y 0..31.
        """))
        lay.addRow(help_box)

    def refresh_from_model(self):
        # Do NOT override user edits-in-progress: only push values when the spinbox isn't focused.
        cur_loc = self.model.u8(FIELDS["party_location"].offset)
        idx = self.loc_combo.findData(cur_loc)
        if idx >= 0 and self.loc_combo.currentIndex() != idx:
            self.loc_combo.blockSignals(True)
            self.loc_combo.setCurrentIndex(idx)
            self.loc_combo.blockSignals(False)

        if not self.z_spin._spin.hasFocus():
            self.z_spin.blockSignals(True)
            self.z_spin.setValue(self.model.u8(FIELDS["party_z"].offset))
            self.z_spin.blockSignals(False)
        if not self.x_spin._spin.hasFocus():
            self.x_spin.blockSignals(True)
            self.x_spin.setValue(self.model.u8(FIELDS["party_x"].offset))
            self.x_spin.blockSignals(False)
        if not self.y_spin._spin.hasFocus():
            self.y_spin.blockSignals(True)
            self.y_spin.setValue(self.model.u8(FIELDS["party_y"].offset))
            self.y_spin.blockSignals(False)

        if not self.transport._spin.hasFocus():
            self.transport.blockSignals(True)
            self.transport.setValue(self.model.u8(FIELDS["transport_mode"].offset))
            self.transport.blockSignals(False)
        if not self.active_char._spin.hasFocus():
            self.active_char.blockSignals(True)
            self.active_char.setValue(self.model.u8(FIELDS["active_char"].offset))
            self.active_char.blockSignals(False)

        # Keep num_party in sync with actual in-party flags (in case file changed elsewhere)
        counted = 0
        for i in range(16):
            base = 0x02 + i*32
            # name preview
            raw = bytes(self.model.bytes[base:base+8])
            name = raw.split(b'\x00', 1)[0].decode('ascii', errors='ignore')
            self.members_tbl.item(i, 1).setText(name if name else f"Char {i+1}")
            # in-party
            in_party = (self.model.u8(base + 0x1F) == 0x00)
            self._member_inparty[i].blockSignals(True)
            self._member_inparty[i].setChecked(in_party)
            self._member_inparty[i].blockSignals(False)
            if in_party:
                counted += 1
            # status
            stat_byte = self.model.u8(base + 0x0B)
            combo = self._member_status[i]
            found = False
            for j in range(combo.count()):
                if combo.itemData(j) == stat_byte:
                    combo.blockSignals(True); combo.setCurrentIndex(j); combo.blockSignals(False); found = True; break
            if not found:
                combo.blockSignals(True)
                # ensure first item reflects the raw byte
                if combo.count() == 0 or combo.itemData(0) != stat_byte:
                    combo.insertItem(0, f"0x{stat_byte:02X}", stat_byte)
                combo.setCurrentIndex(0)
                combo.blockSignals(False)
            # HPs
            cur_hp = self.model.u16(base + 0x10)
            max_hp = self.model.u16(base + 0x12)
            self._member_hp_cur[i].blockSignals(True); self._member_hp_cur[i].setValue(cur_hp); self._member_hp_cur[i].blockSignals(False)
            self._member_hp_max[i].blockSignals(True); self._member_hp_max[i].setValue(max_hp); self._member_hp_max[i].blockSignals(False)

        if not self.num_party._spin.hasFocus():
            self.num_party.blockSignals(True)
            self.num_party.setValue(counted if 1 <= counted <= 6 else self.model.u8(FIELDS["num_party"].offset))
            self.num_party.blockSignals(False)

    def on_apply(self, *args):
        # Push current UI values into the model (and thus the bytearray)
        self.model.set_u8(FIELDS["party_location"].offset, int(self.loc_combo.currentData()))
        self.model.set_u8(FIELDS["party_z"].offset, self.z_spin.value())
        self.model.set_u8(FIELDS["party_x"].offset, self.x_spin.value())
        self.model.set_u8(FIELDS["party_y"].offset, self.y_spin.value())
        self.model.set_u8(FIELDS["transport_mode"].offset, self.transport.value())
        self.model.set_u8(FIELDS["active_char"].offset, self.active_char.value())
        # Keep member count coherent if user adjusted "In Party" checkboxes separately
        self.model.set_u8(FIELDS["num_party"].offset, self.num_party.value())


class TimeTab(QtWidgets.QWidget):
    def __init__(self, model: InitGamModel):
        super().__init__(); self.model = model
        lay = QtWidgets.QFormLayout(self)
        self.month = LabeledSpin("Month (0x2D7)", model.u8(FIELDS["month"].offset), 1, 13, FIELDS["month"].tooltip)
        self.day = LabeledSpin("Day (0x2D8)", model.u8(FIELDS["day"].offset), 1, 28, FIELDS["day"].tooltip)
        self.hour = LabeledSpin("Hour (0x2D9)", model.u8(FIELDS["hour"].offset), 0, 23, FIELDS["hour"].tooltip)
        self.minute = LabeledSpin("Minute (0x2DB)", model.u8(FIELDS["minute"].offset), 0, 59, FIELDS["minute"].tooltip)
        self.karma = LabeledSpin("Karma (0x2E2)", model.u8(FIELDS["karma"].offset), 0, 255, FIELDS["karma"].tooltip)
        self.trammel = LabeledSpin("Trammel phase (0x2DF)", model.u8(FIELDS["trammel"].offset), 0, 255, FIELDS["trammel"].tooltip)
        self.felucca = LabeledSpin("Felucca phase (0x2E0)", model.u8(FIELDS["felucca"].offset), 0, 255, FIELDS["felucca"].tooltip)
        for w in [self.month, self.day, self.hour, self.minute, self.karma, self.trammel, self.felucca]: lay.addRow(w)
        for w, fld in [(self.month,"month"),(self.day,"day"),(self.hour,"hour"),(self.minute,"minute"),
                       (self.karma,"karma"),(self.trammel,"trammel"),(self.felucca,"felucca")]:
            w.valueChanged.connect(lambda _=None, key=fld: self.apply_field(key))
        self.model.data_changed.connect(self.refresh_from_model)
        help_box = QtWidgets.QTextEdit(); help_box.setReadOnly(True)
        help_box.setStyleSheet("QTextEdit { background: #f7f7f7; }")
        help_box.setText("Adjust in-game time and karma. Moongate phases influence destinations at specific hours.")
        lay.addRow(help_box)
    def apply_field(self, key:str):
        f = FIELDS[key]; val = getattr(self, key).value()
        (self.model.set_u8 if f.length==1 else self.model.set_u16)(f.offset, val)
    def refresh_from_model(self):
        self.month.setValue(self.model.u8(FIELDS["month"].offset))
        self.day.setValue(self.model.u8(FIELDS["day"].offset))
        self.hour.setValue(self.model.u8(FIELDS["hour"].offset))
        self.minute.setValue(self.model.u8(FIELDS["minute"].offset))
        self.karma.setValue(self.model.u8(FIELDS["karma"].offset))
        self.trammel.setValue(self.model.u8(FIELDS["trammel"].offset))
        self.felucca.setValue(self.model.u8(FIELDS["felucca"].offset))

class InventoryTab(QtWidgets.QWidget):
    def __init__(self, model: InitGamModel):
        super().__init__(); self.model = model
        main = QtWidgets.QVBoxLayout(self)
        group1 = QtWidgets.QGroupBox("Supplies"); f1 = QtWidgets.QFormLayout(group1)
        self.food = LabeledSpin("Food (0x0202)", model.u16(FIELDS["food"].offset), 0, 9999, "Party food.")
        self.gold = LabeledSpin("Gold (0x0204)", model.u16(FIELDS["gold"].offset), 0, 9999, "Gold on hand.")
        self.keys = LabeledSpin("Keys (0x0206)", model.u8(0x206), 0, 99, "0–99")
        self.gems = LabeledSpin("Gems (0x0207)", model.u8(0x207), 0, 99, "0–99")
        self.torches = LabeledSpin("Torches (0x0208)", model.u8(0x208), 0, 99, "0–99")
        for w in [self.food, self.gold, self.keys, self.gems, self.torches]: f1.addRow(w)
        main.addWidget(group1)

        group2 = QtWidgets.QGroupBox("Plot Items / Flags (0x209..0x219)"); g2 = QtWidgets.QGridLayout(group2)
        self.flag_widgets: Dict[str, QtWidgets.QComboBox] = {}; row = 0
        for key, fld in PLOT_FLAGS.items():
            cb = QtWidgets.QComboBox(); cb.addItems(["0x00 (not owned)","0xFF (owned)"])
            cb.setCurrentIndex(1 if self.model.u8(fld.offset)==0xFF else 0)
            cb.setToolTip(f"{fld.name}: {fld.tooltip}")
            lbl = QtWidgets.QLabel(f"{fld.name} ({fld.offset:#06x})"); lbl.setToolTip(fld.tooltip)
            g2.addWidget(lbl, row, 0); g2.addWidget(cb, row, 1)
            cb.currentIndexChanged.connect(lambda _, f=fld, k=key: self.model.set_u8(f.offset, 0xFF if self.flag_widgets[k].currentIndex()==1 else 0x00))
            self.flag_widgets[key] = cb; row += 1
        main.addWidget(group2)

        group3 = QtWidgets.QGroupBox("Reagents (0x2AA..0x2B1)"); f3 = QtWidgets.QFormLayout(group3)
        self.reagent_spins: Dict[str, LabeledSpin] = {}
        for name, off in REAGENTS:
            sp = LabeledSpin(f"{name} ({off:#06x})", self.model.u8(off), 0, 99, "0–99")
            sp.valueChanged.connect(lambda _, o=off, n=name: self.model.set_u8(o, self.reagent_spins[n].value()))
            f3.addRow(sp); self.reagent_spins[name] = sp
        main.addWidget(group3)

        self.model.data_changed.connect(self.refresh_from_model)

    def refresh_from_model(self):
        self.food.setValue(self.model.u16(FIELDS["food"].offset)); self.gold.setValue(self.model.u16(FIELDS["gold"].offset))
        self.keys.setValue(self.model.u8(0x206)); self.gems.setValue(self.model.u8(0x207)); self.torches.setValue(self.model.u8(0x208))
        for key, fld in PLOT_FLAGS.items(): self.flag_widgets[key].setCurrentIndex(1 if self.model.u8(fld.offset)==0xFF else 0)
        for name, off in REAGENTS: self.reagent_spins[name].setValue(self.model.u8(off))

class MoonstoneTab(QtWidgets.QWidget):
    def __init__(self, model: InitGamModel):
        super().__init__(); self.model = model
        lay = QtWidgets.QVBoxLayout(self)
        self.table = QtWidgets.QTableWidget(8, 4)
        self.table.setHorizontalHeaderLabels(["X (0x28A+)", "Y (0x292+)", "Flag 0=buried 0xFF=in inv (0x29A+)", "Z 0=Brit 0xFF=Under (0x2A2+)"])
        self.table.horizontalHeader().setSectionResizeMode(QtWidgets.QHeaderView.Stretch)
        lay.addWidget(self.table)
        self.refresh_from_model(); self.table.itemChanged.connect(self.on_item_changed)
        self.model.data_changed.connect(self.refresh_from_model)
        help_box = QtWidgets.QTextEdit(); help_box.setReadOnly(True)
        help_box.setStyleSheet("QTextEdit { background: #f7f7f7; }")
        help_box.setText("Each stone has X/Y; Flag=0 buried (then Z matters), 0xFF in inventory.")
        lay.addWidget(help_box)

    def refresh_from_model(self):
        self.table.blockSignals(True)
        for i in range(8):
            vals = [self.model.u8(MOONSTONE_BASES["x"]+i), self.model.u8(MOONSTONE_BASES["y"]+i),
                    self.model.u8(MOONSTONE_BASES["flag"]+i), self.model.u8(MOONSTONE_BASES["z"]+i)]
            for col, val in enumerate(vals):
                it = self.table.item(i, col)
                if it is None:
                    it = QtWidgets.QTableWidgetItem(str(val)); it.setData(QtCore.Qt.EditRole, int(val))
                    self.table.setItem(i, col, it)
                else:
                    it.setText(str(val))
        self.table.blockSignals(False)

    def on_item_changed(self, item: QtWidgets.QTableWidgetItem):
        row, col = item.row(), item.column()
        try: val = int(item.text())
        except ValueError: return
        val = clamp(val, 0, 255)
        base = [MOONSTONE_BASES["x"], MOONSTONE_BASES["y"], MOONSTONE_BASES["flag"], MOONSTONE_BASES["z"]][col]
        self.model.set_u8(base + row, val)

class FlagsTab(QtWidgets.QWidget):
    def __init__(self, model: InitGamModel):
        super().__init__()
        self.model = model
        main = QtWidgets.QVBoxLayout(self)

        # Shrine quest flags (existing block)
        groupS = QtWidgets.QGroupBox("Shrine Quest Flags (bits 0..7 map to virtues)")
        gl = QtWidgets.QGridLayout(groupS)
        self.shrine_checks: Dict[Tuple[str,int], QtWidgets.QCheckBox] = {}
        r=0
        for name, off in SHRINE_FLAGS.items():
            gl.addWidget(QtWidgets.QLabel(f"{name} ({off:#06x})"), r, 0, 1, 2)
            r+=1
            for b, virt in enumerate(SHRINE_BITS):
                cb = QtWidgets.QCheckBox(virt)
                cb.setToolTip(f"{name}: set bit if {virt} is active/completed.")
                gl.addWidget(cb, r, b)
                self.shrine_checks[(name, b)] = cb
            r+=1
        main.addWidget(groupS)

        # Dungeon open flags (existing block)
        groupD = QtWidgets.QGroupBox("Dungeon Open Flags (0x32A..0x331)")
        gd = QtWidgets.QGridLayout(groupD)
        self.dung_combo: List[QtWidgets.QComboBox] = []
        for i, dn in enumerate(DUNGEON_NAMES):
            lbl = QtWidgets.QLabel(f"{dn}")
            cb = QtWidgets.QComboBox()
            cb.addItems(["Sealed (0x00)","Open (bit7=1 i.e. 0x80)"])
            gd.addWidget(lbl, i, 0); gd.addWidget(cb, i, 1)
            self.dung_combo.append(cb)
        main.addWidget(groupD)

        # Shrine destroyed statuses (existing block)
        groupSD = QtWidgets.QGroupBox("Shrine Status (0x332..0x339; bit7=1 => destroyed)")
        gs = QtWidgets.QGridLayout(groupSD)
        self.shrine_status: List[QtWidgets.QComboBox] = []
        for i, virt in enumerate(SHRINE_BITS):
            lbl = QtWidgets.QLabel(virt)
            cb = QtWidgets.QComboBox(); cb.addItems(["OK (bit7=0)","Destroyed (bit7=1)"])
            gs.addWidget(lbl, i, 0); gs.addWidget(cb, i, 1)
            self.shrine_status.append(cb)
        main.addWidget(groupSD)

        # --- NEW: Shadowlords visiting towns toggle ---
        groupSL = QtWidgets.QGroupBox("Shadowlords: prevent town visits (0x322..0x324)")
        gl2 = QtWidgets.QGridLayout(groupSL)
        self.sl_checks = []  # [(label, offset, checkbox)]
        sl_meta = [("Falsehood (Faulinei)", 0x322), ("Hatred (Astaroth)", 0x323), ("Cowardice (Nosfentor)", 0x324)]
        for i, (label, off) in enumerate(sl_meta):
            cb = QtWidgets.QCheckBox("Prevent visits (set 0xFF)")
            gl2.addWidget(QtWidgets.QLabel(f"{label}"), i, 0)
            gl2.addWidget(cb, i, 1)
            self.sl_checks.append((off, cb))
            cb.stateChanged.connect(lambda _=None, ii=i: (
                self.model.set_u8(self.sl_checks[ii][0],
                                  0xFF if self.sl_checks[ii][1].isChecked() else 0x00)
            ))
        note = QtWidgets.QLabel("Tip: 0xFF disables town visits. Non-0xFF values (0..8) allow visits; we restore 0x00 when re-enabling.")
        note.setWordWrap(True)
        gl2.addWidget(note, len(sl_meta), 0, 1, 2)
        main.addWidget(groupSL)

        # Bindings (existing)
        self.refresh_from_model()
        for key, cb in self.shrine_checks.items():
            cb.stateChanged.connect(lambda _=None, k=key: self.apply_shrine_bit(k))
        for i, cb in enumerate(self.dung_combo):
            cb.currentIndexChanged.connect(lambda _=None, idx=i: self.apply_dungeon(idx))
        for i, cb in enumerate(self.shrine_status):
            cb.currentIndexChanged.connect(lambda _=None, idx=i: self.apply_shrine_status(idx))

        self.model.data_changed.connect(self.refresh_from_model)

    def refresh_from_model(self):
        # Shrine flags (existing)
        for name, off in SHRINE_FLAGS.items():
            byte = self.model.u8(off)
            for b in range(8):
                self.shrine_checks[(name,b)].blockSignals(True)
                self.shrine_checks[(name,b)].setChecked(bool(byte & (1<<b)))
                self.shrine_checks[(name,b)].blockSignals(False)

        # Dungeons (existing)
        for i in range(8):
            val = self.model.u8(DUNGEON_FLAGS_OFFSET + i)
            open_bit = 1 if (val & 0x80) else 0
            self.dung_combo[i].blockSignals(True)
            self.dung_combo[i].setCurrentIndex(open_bit)
            self.dung_combo[i].blockSignals(False)

        # Shrine destroyed (existing)
        for i in range(8):
            val = self.model.u8(SHRINE_STATUS_OFFSET + i)
            destroyed = 1 if (val & 0x80) else 0
            self.shrine_status[i].blockSignals(True)
            self.shrine_status[i].setCurrentIndex(destroyed)
            self.shrine_status[i].blockSignals(False)

        # NEW: Shadowlords visiting towns
        for off, cb in self.sl_checks:
            v = self.model.u8(off)
            cb.blockSignals(True)
            cb.setChecked(v == 0xFF)  # checked = prevented (0xFF)
            cb.blockSignals(False)

    def apply_shrine_bit(self, key:Tuple[str,int]):
        name, bit = key; off = SHRINE_FLAGS[name]; byte = self.model.u8(off)
        byte = (byte | (1<<bit)) if self.shrine_checks[key].isChecked() else (byte & ~(1<<bit))
        self.model.set_u8(off, byte)

    def apply_dungeon(self, idx:int):
        off = DUNGEON_FLAGS_OFFSET + idx; val = self.model.u8(off)
        val = ((val | 0x80) & 0xFF) if self.dung_combo[idx].currentIndex()==1 else (val & 0x7F)
        self.model.set_u8(off, val)

    def apply_shrine_status(self, idx:int):
        off = SHRINE_STATUS_OFFSET + idx; val = self.model.u8(off)
        val = ((val | 0x80) & 0xFF) if self.shrine_status[idx].currentIndex()==1 else (val & 0x7F)
        self.model.set_u8(off, val)

class RawBytesTab(QtWidgets.QWidget):
    def __init__(self, model: InitGamModel):
        super().__init__(); self.model = model
        lay = QtWidgets.QVBoxLayout(self)
        self.view = QtWidgets.QPlainTextEdit()
        self.view.setFont(QtGui.QFontDatabase.systemFont(QtGui.QFontDatabase.FixedFont))
        self.view.setLineWrapMode(QtWidgets.QPlainTextEdit.NoWrap)
        lay.addWidget(self.view)
        btns = QtWidgets.QHBoxLayout(); self.apply_btn = QtWidgets.QPushButton("Apply Changes from Text"); self.revert_btn = QtWidgets.QPushButton("Revert to File Bytes")
        btns.addStretch(1); btns.addWidget(self.apply_btn); btns.addWidget(self.revert_btn); lay.addLayout(btns)
        self.apply_btn.clicked.connect(self.apply_text); self.revert_btn.clicked.connect(self.refresh_from_model)
        self.model.data_changed.connect(self.refresh_from_model); self.refresh_from_model()
        self.view.setToolTip("Editable hexdump; spaces/newlines ignored. Pairs of hex digits only.")

    def refresh_from_model(self):
        b = bytes(self.model.bytes); lines = []
        for i in range(0, len(b), 16):
            chunk = b[i:i+16]; hexs = " ".join(f"{x:02X}" for x in chunk)
            lines.append(f"{i:08X}  {hexs}")
        self.view.setPlainText("\n".join(lines))

    def apply_text(self):
        text = self.view.toPlainText(); raw = []
        for line in text.splitlines():
            parts = line.strip().split()
            if not parts: continue
            try: int(parts[0], 16); hexparts = parts[1:]
            except ValueError: hexparts = parts
            for tok in hexparts:
                tok = tok.strip().rstrip(",")
                if len(tok) != 2: continue
                try: raw.append(int(tok,16))
                except ValueError: pass
        if raw:
            n = min(len(raw), len(self.model.bytes))
            self.model.bytes[:n] = bytes(raw[:n]); self.model.data_changed.emit()

class HexEditorTab(QtWidgets.QWidget):
    def __init__(self, model: InitGamModel):
        super().__init__(); self.model = model
        lay = QtWidgets.QVBoxLayout(self)
        top = QtWidgets.QHBoxLayout(); self.offset_edit = QtWidgets.QLineEdit("0x0000")
        self.count_spin = QtWidgets.QSpinBox(); self.count_spin.setRange(16, 4096); self.count_spin.setValue(256)
        self.load_btn = QtWidgets.QPushButton("Load Window")
        top.addWidget(QtWidgets.QLabel("Start offset:")); top.addWidget(self.offset_edit)
        top.addWidget(QtWidgets.QLabel("Length:")); top.addWidget(self.count_spin)
        top.addWidget(self.load_btn); top.addStretch(1); lay.addLayout(top)
        self.table = QtWidgets.QTableWidget(0, 17); headers = ["Offset"] + [f"+{i:02X}" for i in range(16)]
        self.table.setHorizontalHeaderLabels(headers); self.table.horizontalHeader().setSectionResizeMode(QtWidgets.QHeaderView.ResizeToContents)
        lay.addWidget(self.table)
        self.load_btn.clicked.connect(self.reload_window); self.table.itemChanged.connect(self.on_item_changed)
        self.model.data_changed.connect(self.reload_window); self.reload_window()
        self.setToolTip("Direct hex window. Edit bytes in hex (00–FF).")

    def parse_offset(self)->int:
        s = self.offset_edit.text().strip()
        try: return int(s, 16) if s.lower().startswith("0x") else int(s)
        except ValueError: return 0

    def reload_window(self):
        off = self.parse_offset(); n = self.count_spin.value()
        self.table.blockSignals(True); self.table.setRowCount(0); data = self.model.bytes
        end = min(off + n, len(data)); i = off
        while i < end:
            row = self.table.rowCount(); self.table.insertRow(row)
            base_lbl = QtWidgets.QTableWidgetItem(f"{i:08X}"); base_lbl.setFlags(QtCore.Qt.ItemIsEnabled); self.table.setItem(row, 0, base_lbl)
            for j in range(16):
                idx = i + j; it = QtWidgets.QTableWidgetItem("")
                if idx < len(data): it.setText(f"{data[idx]:02X}")
                self.table.setItem(row, 1+j, it)
            i += 16
        self.table.blockSignals(False)

    def on_item_changed(self, item: QtWidgets.QTableWidgetItem):
        row, col = item.row(), item.column()
        if col == 0: return
        try: base_off = int(self.table.item(row, 0).text(), 16)
        except Exception: return
        idx = base_off + (col-1)
        try: val = int(item.text(), 16) & 0xFF
        except ValueError: return
        if idx >= 0:
            self.model.set_u8(idx, val)

class MainWindow(QtWidgets.QMainWindow):
    def __init__(self, path: Optional[str]=None):
        super().__init__()
        self.setWindowTitle("Ultima V INIT.GAM Editor")
        self.resize(1100, 800)
        self.model = InitGamModel(Path(path) if path else None)
        tabs = QtWidgets.QTabWidget(); tabs.setTabPosition(QtWidgets.QTabWidget.North)
        tabs.addTab(PartyTab(self.model), "Party & Start")
        tabs.addTab(TimeTab(self.model), "Time & Karma")
        tabs.addTab(InventoryTab(self.model), "Inventory")
        tabs.addTab(MoonstoneTab(self.model), "Moonstones")
        tabs.addTab(FlagsTab(self.model), "Flags")
        tabs.addTab(RawBytesTab(self.model), "Raw Dump")
        tabs.addTab(HexEditorTab(self.model), "Hex Editor")
        self.setCentralWidget(tabs)
        tb = self.addToolBar("File"); act_open = QtGui.QAction("Open...", self); act_save = QtGui.QAction("Save", self); act_saveas = QtGui.QAction("Save As...", self)
        tb.addAction(act_open); tb.addAction(act_save); tb.addAction(act_saveas)
        act_open.triggered.connect(self.on_open); act_save.triggered.connect(self.on_save); act_saveas.triggered.connect(self.on_save_as)
        self.help_label = QtWidgets.QLabel("Open an INIT.GAM (File → Open) or pass it on the command line."); self.statusBar().addPermanentWidget(self.help_label, 1)
        dock = QtWidgets.QDockWidget("INIT.GAM Overview", self)
        helpw = QtWidgets.QTextEdit(); helpw.setReadOnly(True); helpw.setStyleSheet("QTextEdit { background: #fcfcfc; }")
        helpw.setText(textwrap.dedent("""\
            INIT.GAM is the baseline RAM/save-image copied to SAVED.GAM at New Game.
            Key offsets:
             • 0x02ED location; 0x02EF–0x02F1 Z/X/Y
             • 0x0202/0x0204 food/gold (uint16 LE)
             • 0x02D7–0x02DB time; 0x02E2 karma
             • 0x0209..0x0219 plot item flags (0xFF=owned)
             • 0x028A.. moonstones; 0x0326/0x0328 shrine quest bits; 0x032A.. dungeons; 0x0332.. shrine status
            The intro may overwrite the start position; skip/patch intro to keep your custom start.
        """)); dock.setWidget(helpw); self.addDockWidget(QtCore.Qt.RightDockWidgetArea, dock)

    def on_open(self):
        path, _ = QtWidgets.QFileDialog.getOpenFileName(self, "Open INIT.GAM", "", "INIT.GAM (INIT.GAM);;All files (*)")
        if not path: return
        self.model.load(Path(path))

    def on_save(self):
        try: self.model.save(); QtWidgets.QMessageBox.information(self, "Saved", f"Saved to {self.model.path}")
        except Exception as e: QtWidgets.QMessageBox.critical(self, "Error", str(e))

    def on_save_as(self):
        path, _ = QtWidgets.QFileDialog.getSaveFileName(self, "Save As", "INIT_edited.GAM", "GAM files (*.GAM);;All files (*)")
        if not path: return
        try: self.model.save(Path(path)); QtWidgets.QMessageBox.information(self, "Saved", f"Saved to {path}")
        except Exception as e: QtWidgets.QMessageBox.critical(self, "Error", str(e))

def main():
    app = QtWidgets.QApplication(sys.argv)
    path = sys.argv[1] if len(sys.argv) > 1 else None
    win = MainWindow(path); win.show(); sys.exit(app.exec())

if __name__ == "__main__":
    main()
