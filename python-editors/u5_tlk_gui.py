# u5_tlk_gui.py
# Ultima V TLK GUI editor (PyQt6) with structured conversation tree.
# - Hides <Or> operator lines in the Conversation structure view.
# - Trigger groups under Labels: show all Trigger lines first, then ONE Response.
# - Adding a Trigger via context menu auto-inserts a hidden <Or> when appending
#   to an existing trigger group (kept in data, not shown in tree).
# - Auto-apply edits on navigation / before save.
# - Resizable panes via splitters.
# - NPC list shows Name (trims at first quote).
# - Byte-perfect round-trip; editor view uses token-aware pretty decode from bytes.

from __future__ import annotations
from pathlib import Path
from u5_formats import tlk_header, tlk_segments, build_tlk, TAIL
import sys, struct, re
from typing import List, Tuple, Optional, Dict
from PyQt6 import QtWidgets, QtGui, QtCore

# -------------------------
# Compat token tables (includes "may" fix)
# -------------------------
# Token IDs are one-based indexes into src/vars.c D_24ea.
TOKEN_WORDS = ['the', 'thou', 'of', 'to', 'and', 'that', 'for', None, 'in', 'is', 'have', 'with', 'thee', 'this',
 'not', 'my', 'it', 'me', 'but', 'dost', 'know', 'be', 'was', 'Blackthorn', 'from', 'thy', 'one',
 None, 'are', 'here', 'many', 'Lord', 'am', 'we', 'they', 'he', 'would', 'art', 'on', 'young',
 'what', 'see', 'like', 'only', 'by', 'there', "Blackthorn's", 'good', 'been', None, 'must', 'his',
 'British', 'fine', 'an', 'great', 'thee,', 'our', 'who', 'name', 'heard', 'as', 'at', 'has', None,
 'through', None, 'once', 'can', None, 'him', None, None, None, None, 'ye', 'Shadowlords', 'tell',
 'some', 'believe', 'all', 'their', 'upon', 'even', "'tis", 'find', 'if', 'about', "don't",
 'before', 'these', 'just', 'make', 'will', 'when', 'three', 'Great', 'might', 'those', 'old',
 'hast', 'ask', 'unto', 'wish', 'man', 'so', 'knows', 'still', 'Mantra', 'out', 'help', 'well',
 'shall', 'think', 'where', 'named', 'talking', 'more', 'such', 'very', 'may', 'lives', 'canst',
 'which', 'since', 'need', "I've", 'work']
TOKEN_BY_CODE = {i+1: w for i, w in enumerate(TOKEN_WORDS) if w is not None}
WORDMAP = {w.lower(): code for code, w in TOKEN_BY_CODE.items()}
WORDMAP["<avatar>"] = 129

CTRL_TO_BYTE = {
    "<End Conversation>": 130,
    "<Pause>": 131,
    "<Join Party>": 132,
    "<Or>": 135,
    "<Ask Name>": 136,
    "<Karma + 1>": 137,
    "<Karma - 1>": 138,
    "<Call Guards>": 139,
    "<Set Flag>": 140,
    "<New Line>": 141,
    "<Rune>": 142,
    "<Key Wait>": 143,
    "<Any>": 144,
    "<Avatar>": 129,
}

# -------------------------
# TLK codec (compat)
# -------------------------

def is_word_char(ch: str) -> bool:
    return ch.isalnum() or ch == "'"

def try_emit_token_word(s: str, pos: int, out: bytearray) -> int:
    # strict, longest-first, exact canonical, space-bounded
    maxlen = min(24, len(s) - pos)
    for L in range(maxlen, 0, -1):
        cand = s[pos:pos+L]
        b = WORDMAP.get(cand.lower())
        if b is None:
            continue
        # find canonical form
        canonical = TOKEN_BY_CODE.get(b)
        if canonical is None or cand != canonical:
            continue
        prev = s[pos-1] if pos > 0 else None
        nxt  = s[pos+L] if pos+L < len(s) else None
        if prev is not None and prev != ' ':
            continue
        if nxt is not None and nxt != ' ':
            continue
        out.append(b)
        return pos + L
    return -1

TAG_RE_LABEL = re.compile(r'^<\s*(?:goto\s+)?label\s+([1-9]|1[0-5])\s*>$', re.I)
TAG_RE_GOLD  = re.compile(r'^<\s*gold\s*-\s*([0-9]{3})\s*>$', re.I)
TAG_RE_ITEM  = re.compile(r'^<\s*item\s*:\s*([0-9]{1,3})\s*>$', re.I)
TAG_RE_WORD  = re.compile(r'^<\s*word\s*:\s*([0-9]{1,3})\s*>$', re.I)
TAG_RE_UNK   = re.compile(r'^<\s*unknown\s*:\s*([0-9]{1,3})\s*>$', re.I)

def try_emit_tag(s: str, pos: int, out: bytearray) -> int:
    if s[pos] != '<':
        return -1
    end = s.find('>', pos)
    if end == -1:
        return -1
    tag = s[pos:end+1]
    low = tag.lower()
    token = next((code for code, word in TOKEN_BY_CODE.items()
                  if tag[1:-1] == word), None)
    if token is not None:
        out.append(token)
        return end + 1

    for k, b in CTRL_TO_BYTE.items():
        if low == k.lower():
            out.append(b)
            return end + 1

    m = TAG_RE_LABEL.match(tag)
    if m:
        n = int(m.group(1))
        out.append(144 + n)  # 145..155
        return end + 1

    m = TAG_RE_GOLD.match(tag)
    if m:
        out.append(133)
        for d in m.group(1):
            out.append(ord(d) + 128)
        return end + 1

    if low == "<change>":
        out.append(134)
        return end + 1
    m = TAG_RE_ITEM.match(tag)
    if m:
        value = int(m.group(1))
        if not 1 <= value <= 255:
            raise ValueError("Dialogue byte must be 1..255; NUL separates entries")
        out.append(value)
        return end + 1

    for rx in (TAG_RE_WORD, TAG_RE_UNK):
        m = rx.match(tag)
        if m:
            value = int(m.group(1))
            if not 1 <= value <= 255:
                raise ValueError("Dialogue byte must be 1..255; NUL separates entries")
            out.append(value)
            return end + 1

    return -1

def encode_entry(text: str) -> bytes:
    # New label definitions need the Any+label marker used by TALK_0c5c.
    definition = re.match(r"^<Label ([1-9]|1[0-5])>(.+)$", text, re.S)
    out = bytearray([144]) if definition else bytearray()
    i = 0
    while i < len(text):
        c = text[i]
        if c == '<':
            nxt = try_emit_tag(text, i, out)
            if nxt != -1:
                i = nxt
                continue
        uc = ord(c)
        if c == '\n':
            out.append(141)
        elif 32 <= uc <= 126:
            out.append(uc + 128)
        else:
            raise ValueError('Dialogue supports ASCII text and explicit control tags')
        i += 1
    validate_entry(bytes(out))
    out.append(0)
    return bytes(out)

def validate_entry(data: bytes):
    i = 0
    while i < len(data):
        length = {133:3, 134:1, 140:1, 254:2}.get(data[i], 0)
        if i+length >= len(data) or 0 in data[i:i+length+1]:
            raise ValueError("Truncated dialogue opcode or embedded NUL")
        i += 1+length

def count_label_occurrences(entries: List[bytes]) -> Dict[int,int]:
    counts = {}
    for entry in entries:
        i = 0
        while i < len(entry):
            code = entry[i]
            if 145 <= code <= 159:
                counts[code-144] = counts.get(code-144, 0)+1
            i += 1 + {133: 3, 134: 1, 140: 1, 254: 2}.get(code, 0)
    return counts

def decode_entry(data: bytes, label_remaining: Dict[int,int]) -> str:
    out, i = [], 0
    controls = {value: tag for tag, value in CTRL_TO_BYTE.items()}
    while i < len(data):
        code = data[i]
        if code == 0:
            break
        if code in (133, 134, 140, 254):
            length = {133:3, 134:1, 140:1, 254:2}[code]
            operands = data[i+1:i+1+length]
            if len(operands) != length or 0 in operands:
                raise ValueError("Truncated dialogue opcode operands")
            if code == 133 and all(176 <= b <= 185 for b in operands):
                out.append("<Gold - " + ''.join(chr(b-128) for b in operands) + ">")
            else:
                out.append({134:"<Change>",140:"<Set Flag>"}.get(code, f"<Unknown: {code}>"))
                out.extend(f"<Item: {b}>" if code == 134 else f"<Unknown: {b}>" for b in operands)
            i += 1+length
            continue
        if 145 <= code <= 159:
            n = code-144
            remaining = max(0, label_remaining.get(n, 0)-1)
            label_remaining[n] = remaining
            out.append(f"<Label {n}>" if i > 0 and data[i-1] == 144 else f"<Goto Label {n}>")
        elif code in controls:
            out.append(controls[code])
        elif code < 129:
            # Explicit IDs avoid merging adjacent compressed words on re-encode.
            word = TOKEN_BY_CODE.get(code)
            out.append(f"<{word}>" if word else f"<Unknown: {code}>")
        elif 160 <= code <= 253:
            out.append(chr(code-128))
        else:
            out.append(f"<Unknown: {code}>")
        i += 1
    return ''.join(out)

def pretty_decode_bytes(data: bytes, force_label: bool = False) -> str:
    # Keep token boundaries explicit so typing elsewhere cannot change opcode bytes.
    # The conversation tree expands word tags for readability.
    remaining = count_label_occurrences([data])
    if not force_label:
        remaining = {n: count+1 for n, count in remaining.items()}
    return decode_entry(data, remaining)

def read_tlk(path: str) -> Tuple[List[Tuple[int,int]], bytes]:
    blob = Path(path).read_bytes()
    return tlk_header(blob), blob

def extract_npc_entries(headers, blob: bytes) -> List[Tuple[int, List[bytes]]]:
    result = []
    for (npc, _), segment in zip(headers, tlk_segments(headers, blob)):
        # A terminator followed by the final Any/label/@ tail is not an entry.
        trimmed = segment.rstrip(b"\x00")
        if trimmed.endswith(TAIL):
            segment = trimmed[:-len(TAIL)]
        parts = segment.split(b"\x00")
        if parts[-1] == b"":
            parts.pop()
        result.append((npc, parts))
    return result

def write_tlk(path: str, npc_entries: List[Tuple[int, List[bytes]]], original_npcs=None, original_blob=None) -> None:
    segments = []
    originals = tlk_segments(tlk_header(original_blob), original_blob) if original_blob is not None else []
    for index, (_, entries) in enumerate(npc_entries):
        if any(b"\x00" in entry for entry in entries):
            raise ValueError("Embedded NUL in a dialogue entry")
        segment = (originals[index] if original_npcs is not None and
                   npc_entries[index] == original_npcs[index] else
                   b"".join(entry+b"\x00" for entry in entries)+TAIL)
        if len(segment) > 1024:
            raise ValueError("An NPC conversation exceeds the engine's 1024-byte read buffer")
        segments.append(segment)
    if len(npc_entries) > 127:
        raise ValueError("TLK header exceeds the engine's 512-byte buffer")
    Path(path).write_bytes(build_tlk([(npc,0) for npc,_ in npc_entries], segments))

FIRST5 = ["Name", "Description", "Greeting", "Job", "Bye"]

def is_label_start(txt: str) -> Optional[int]:
    # Consider both <Label N> and <Goto Label N> as a "label header" IFF
    # there is body text after the tag. Pure "<Goto Label N>" lines are jumps.
    m = re.match(r'^\s*(?:<Any>\s*)?<\s*(?:goto\s+)?label\s+([1-9]|1[0-5])\s*>\s*(.*)$',
                 txt, flags=re.I | re.S)
    if not m:
        return None
    body = m.group(2)
    return int(m.group(1)) if body.strip() != "" else None

def is_goto_label(txt: str) -> Optional[int]:
    m = re.search(r'<\s*goto\s+label\s+([1-9]|1[0-5])\s*>', txt, flags=re.I)
    if m: return int(m.group(1))
    return None

def is_or(txt: str) -> bool:
    return txt.strip().lower() == "<or>"

def looks_like_keyword(txt: str) -> bool:
    s = txt.strip()
    for code, word in TOKEN_BY_CODE.items():
        s = s.replace(f"<{word}>", word)
    if not s or '<' in s or '\n' in s or ' ' in s:
        return False
    return all(c.isalnum() or c=="'" for c in s) and len(s) <= 12

def is_trigger_in_label(txt: str) -> bool:
    s = txt.strip()
    if s.lower() == "<any>":
        return True
    return looks_like_keyword(s)

ANY_TAIL_RE = re.compile(r'(?:\s*\n)?\s*<\s*Any\s*>\s*$', re.I)
def split_any_marker(txt: str) -> tuple[str, bool]:
    prefix = re.match(r"^\s*<Any>", txt, re.I)
    if prefix:
        return txt[prefix.end():].lstrip(), True
    m = ANY_TAIL_RE.search(txt)
    if not m:
        return txt, False
    pre = txt[:m.start()].rstrip()
    return pre, True

class TreeEntryItem(QtWidgets.QTreeWidgetItem):
    EntryIndexRole = QtCore.Qt.ItemDataRole.UserRole + 1
    def set_entry_index(self, idx: Optional[int]):
        self.setData(0, self.EntryIndexRole, -1 if idx is None else int(idx))
    def entry_index(self) -> int:
        v = self.data(0, self.EntryIndexRole)
        try: return int(v)
        except Exception: return -1

# -------------------------
# GUI / Model
# -------------------------

class TLKModel(QtCore.QObject):
    dataChanged = QtCore.pyqtSignal()
    def __init__(self):
        super().__init__()
        self.path = ""
        self.headers: List[Tuple[int,int]] = []
        self.npcs: List[Tuple[int, List[bytes]]] = []   # npc_id, entries (raw bytes)
        self.decoded: List[List[str]] = []              # decoded strings (no token spaces)
        self.entries_raw: List[List[bytes]] = []        # original raw bytes per entry
        self.rendered: List[List[str]] = []             # baseline decoded strings for identity checks

    def load(self, path: str):
        headers, blob = read_tlk(path)
        npcs = extract_npc_entries(headers, blob)
        decoded = []
        for _, entries in npcs:
            remaining = count_label_occurrences(entries)
            decoded.append([decode_entry(entry, remaining) for entry in entries])
        # Install only after all parsing succeeds, retaining the previous document on error.
        self.path, self.headers, self.original_blob = path, headers, blob
        self.npcs = npcs
        self.entries_raw = [entries[:] for _, entries in npcs]
        self.decoded = decoded
        self.rendered = [texts[:] for texts in decoded]
        self.dataChanged.emit()

    def save_as(self, path: str):
        if self.parent() is not None:
            if not self.parent().apply_editor_if_dirty():
                raise ValueError("Fix the invalid dialogue entry before saving")
        if self.decoded == self.rendered:
            Path(path).write_bytes(self.original_blob)
            self.path = path
            return
        new_entries: List[Tuple[int, List[bytes]]] = []
        for npc_idx, ((npc_id, _raw), texts) in enumerate(zip(self.npcs, self.decoded)):
            packed: List[bytes] = []
            for ent_idx, txt in enumerate(texts):
                if npc_idx < len(self.rendered) and ent_idx < len(self.rendered[npc_idx]) \
                   and txt == self.rendered[npc_idx][ent_idx]:
                    packed.append(self.entries_raw[npc_idx][ent_idx])
                else:
                    packed.append(encode_entry(txt)[:-1])  # omit trailing 0; writer adds it
            new_entries.append((npc_id, packed))
        write_tlk(path, new_entries, self.npcs, self.original_blob)
        self.path = path

class MainWindow(QtWidgets.QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("Ultima V TLK Editor (compat)")
        self.resize(1200, 760)
        self.model = TLKModel()
        self.model.setParent(self)
        self.model.dataChanged.connect(self.refresh_ui)

        self._current_edit_idx: Optional[int] = None
        self._current_npc_idx: Optional[int] = None
        self._editor_dirty = False

        self.npcList = QtWidgets.QListWidget()

        # Splitters for resizable panes
        self.splitLeftMid = QtWidgets.QSplitter(QtCore.Qt.Orientation.Horizontal)
        self.splitMidRight = QtWidgets.QSplitter(QtCore.Qt.Orientation.Horizontal)

        self.tree = QtWidgets.QTreeWidget()
        self.tree.setHeaderLabels(["Conversation structure"])
        self.tree.setAlternatingRowColors(True)
        self.tree.itemSelectionChanged.connect(self.on_tree_selection)
        self.tree.setContextMenuPolicy(QtCore.Qt.ContextMenuPolicy.CustomContextMenu)
        self.tree.customContextMenuRequested.connect(self.on_tree_context_menu)

        self.editor = QtWidgets.QPlainTextEdit()
        self.editor.setFont(QtGui.QFontDatabase.systemFont(QtGui.QFontDatabase.SystemFont.FixedFont))
        self.editor.textChanged.connect(self._on_editor_changed)

        # Quick tag buttons
        tagLayout = QtWidgets.QHBoxLayout()
        for tag in [
            "<New Line>", "<Pause>", "<Key Wait>", "<Or>", "<Ask Name>", "<Any>",
            "<End Conversation>", "<Set Flag>", "<Call Guards>",
            "<Karma + 1>", "<Karma - 1>", "<Rune>",
            "<Join Party>", "<Avatar>"
        ]:
            b = QtWidgets.QPushButton(tag)
            b.clicked.connect(lambda _, t=tag: self.insert_tag(t + "<Unknown: 255>" if t == "<Set Flag>" else t))
            tagLayout.addWidget(b)

        goldItemLayout = QtWidgets.QHBoxLayout()
        self.goldSpin = QtWidgets.QSpinBox(); self.goldSpin.setRange(0, 999); self.goldSpin.setValue(3)
        self.itemSpin = QtWidgets.QSpinBox(); self.itemSpin.setRange(1, 255); self.itemSpin.setValue(65)
        bGold = QtWidgets.QPushButton("Insert <Gold - ###>"); bGold.clicked.connect(self.insert_gold)
        bChange = QtWidgets.QPushButton("Insert <Change><Item:#>"); bChange.clicked.connect(self.insert_change_item)
        goldItemLayout.addWidget(QtWidgets.QLabel("Gold:")); goldItemLayout.addWidget(self.goldSpin); goldItemLayout.addWidget(bGold)
        goldItemLayout.addWidget(QtWidgets.QLabel("Item:")); goldItemLayout.addWidget(self.itemSpin); goldItemLayout.addWidget(bChange)

        labelLayout = QtWidgets.QHBoxLayout()
        self.labelSpin = QtWidgets.QSpinBox(); self.labelSpin.setRange(1, 15); self.labelSpin.setValue(1)
        bGoto = QtWidgets.QPushButton("Insert <Goto Label #>")
        bLab  = QtWidgets.QPushButton("Insert <Label N>")
        bGoto.clicked.connect(lambda: self.insert_tag(f"<Goto Label {self.labelSpin.value()}>"))
        bLab.clicked.connect(self.insert_new_label_entry)
        labelLayout.addWidget(QtWidgets.QLabel("Label #:")); labelLayout.addWidget(self.labelSpin); labelLayout.addWidget(bGoto); labelLayout.addWidget(bLab)

        leftWrap = QtWidgets.QWidget(); leftLayout = QtWidgets.QVBoxLayout(leftWrap)
        leftLayout.addWidget(QtWidgets.QLabel("NPCs")); leftLayout.addWidget(self.npcList)

        midWrap = QtWidgets.QWidget(); midLayout = QtWidgets.QVBoxLayout(midWrap)
        midLayout.addWidget(QtWidgets.QLabel("Conversation map")); midLayout.addWidget(self.tree)

        rightWrap = QtWidgets.QWidget(); rightLayout = QtWidgets.QVBoxLayout(rightWrap)
        rightLayout.addWidget(QtWidgets.QLabel("Editor")); rightLayout.addWidget(self.editor)
        rightLayout.addLayout(tagLayout); rightLayout.addLayout(goldItemLayout); rightLayout.addLayout(labelLayout)

        self.splitLeftMid.addWidget(leftWrap); self.splitLeftMid.addWidget(midWrap)
        self.splitLeftMid.setStretchFactor(0, 1); self.splitLeftMid.setStretchFactor(1, 3)
        self.splitMidRight.addWidget(self.splitLeftMid); self.splitMidRight.addWidget(rightWrap)
        self.splitMidRight.setStretchFactor(0, 4); self.splitMidRight.setStretchFactor(1, 3)
        self.setCentralWidget(self.splitMidRight)

        self.npcList.currentRowChanged.connect(self.on_npc_changed)

        fileMenu = self.menuBar().addMenu("&File")
        actOpen = fileMenu.addAction("Open TLK…")
        actSaveAs = fileMenu.addAction("Save As…")
        actQuit = fileMenu.addAction("Quit")
        actOpen.triggered.connect(self.open_tlk); actSaveAs.triggered.connect(self.save_as); actQuit.triggered.connect(self.close)

    # New label entry helpers
    def next_unused_label(self, npc_idx: int) -> Optional[int]:
        if npc_idx < 0 or npc_idx >= len(self.model.decoded):
            return None
        used: set[int] = set()
        rx = re.compile(r'<\s*(?:goto\s+)?label\s+([1-9]|1[0-5])\s*>', flags=re.I)
        for s in self.model.decoded[npc_idx]:
            for m in rx.finditer(s):
                used.add(int(m.group(1)))
        for n in range(1, 16):
            if n not in used:
                return n
        return None

    def insert_new_label_entry(self):
        npc_idx = self.npcList.currentRow()
        if npc_idx < 0 or npc_idx >= len(self.model.decoded):
            QtWidgets.QMessageBox.warning(self, "No NPC Selected", "Select an NPC first.")
            return

        if not self.apply_editor_if_dirty():
            return

        n = self.next_unused_label(npc_idx)
        if n is None:
            QtWidgets.QMessageBox.warning(self, "Labels Full", "All label numbers 1–15 are already used.")
            return

        # IMPORTANT: include a placeholder body so is_label_start() treats it as a header
        placeholder = "New label text"
        self.model.decoded[npc_idx].append(f"<Any><Label {n}>{placeholder}")
        new_idx = len(self.model.decoded[npc_idx]) - 1

        self.populate_tree(npc_idx)
        self.select_tree_entry(new_idx)  # optional: focus the new entry for immediate editing

    def select_tree_entry(self, entry_idx: int):
        def walk(item: QtWidgets.QTreeWidgetItem):
            if item.data(0, TreeEntryItem.EntryIndexRole) == entry_idx:
                self.tree.setCurrentItem(item)
                return True
            for i in range(item.childCount()):
                if walk(item.child(i)):
                    return True
            return False
        for r in range(self.tree.topLevelItemCount()):
            if walk(self.tree.topLevelItem(r)):
                return

    # File actions
    def open_tlk(self):
        path, _ = QtWidgets.QFileDialog.getOpenFileName(self, "Open TLK", "", "TLK Files (*.TLK *.tlk);;All Files (*)")
        if not path: return
        try:
            if not self.apply_editor_if_dirty():
                return
            self.model.load(path)
        except Exception as e:
            QtWidgets.QMessageBox.critical(self, "Error", str(e))

    def save_as(self):
        if not self.model.decoded: return
        path, _ = QtWidgets.QFileDialog.getSaveFileName(self, "Save TLK As", "", "TLK Files (*.TLK *.tlk);;All Files (*)")
        if not path: return
        try:
            if not self.apply_editor_if_dirty():
                return
            self.model.save_as(path)
            QtWidgets.QMessageBox.information(self, "Saved", f"Saved to:\n{path}")
        except Exception as e:
            QtWidgets.QMessageBox.critical(self, "Error", str(e))

    # NPC list name helper
    def npc_display_name(self, npc_idx: int) -> str:
        try:
            raw = self.model.decoded[npc_idx][0]
        except Exception:
            return ""
        if '"' in raw:
            raw = raw.split('"', 1)[0]
        raw = re.sub(r'<[^>]+>', '', raw)
        lines = raw.splitlines()
        return lines[0].strip() if lines else ""

    def refresh_ui(self):
        self.npcList.clear()
        for idx, ((npc_id, _), entries) in enumerate(zip(self.model.npcs, self.model.decoded)):
            name = self.npc_display_name(idx)
            title = f"NPC {npc_id} ({name}, {len(entries)} entries)" if name else f"NPC {npc_id} ({len(entries)} entries)"
            self.npcList.addItem(title)
        if self.npcList.count() > 0:
            self.npcList.setCurrentRow(0)

    # Editor dirty tracking
    def _on_editor_changed(self):
        self._editor_dirty = True

    def apply_editor_if_dirty(self):
        if not self._editor_dirty:
            return True
        if self._current_npc_idx is None or self._current_edit_idx is None or self._current_npc_idx < 0 or self._current_edit_idx < 0:
            self._editor_dirty = False
            return True
        text = self.editor.toPlainText()
        try:
            encode_entry(text)
        except ValueError as error:
            QtWidgets.QMessageBox.warning(self, "Invalid dialogue entry", str(error))
            return False
        self.model.decoded[self._current_npc_idx][self._current_edit_idx] = text
        self._editor_dirty = False
        self.populate_tree(self._current_npc_idx)
        return True

    # -------- Pretty helpers for Conversation structure --------
    def pretty_display(self, npc_idx: int, entry_idx: int, force_label: bool=False) -> str:
        """Pretty-decode the bytes for this entry if unchanged; otherwise pretty-decode
        the freshly re-encoded current text. Display-only."""
        txt = self.model.decoded[npc_idx][entry_idx]
        baseline = None
        if npc_idx < len(self.model.rendered) and entry_idx < len(self.model.rendered[npc_idx]):
            baseline = self.model.rendered[npc_idx][entry_idx]
        if baseline is not None and txt == baseline:
            raw_bytes = self.model.entries_raw[npc_idx][entry_idx]
        else:
            raw_bytes = encode_entry(txt)[:-1]
        return pretty_decode_bytes(raw_bytes, force_label=force_label)

    def strip_label_heading_pretty(self, pretty: str) -> str:
        """Strip either <Label N> or <Goto Label N> at the start of a pretty string."""
        m = re.match(r'^\s*(?:<Any>\s*)?<\s*(?:goto\s+)?label\s+[0-9]+\s*>\s*(.*)$', pretty, flags=re.I|re.S)
        return m.group(1) if m else pretty

    # Tree building
    def on_npc_changed(self, npc_idx: int):
        if not self.apply_editor_if_dirty():
            return
        self._current_npc_idx = npc_idx
        self._current_edit_idx = None
        self.populate_tree(npc_idx)

    def populate_tree(self, npc_idx: int):
        self.tree.clear(); self.editor.clear()
        if npc_idx < 0 or npc_idx >= len(self.model.decoded): return
        entries = self.model.decoded[npc_idx]

        fixedRoot = TreeEntryItem([f"Fixed fields"]); fixedRoot.set_entry_index(None); self.tree.addTopLevelItem(fixedRoot)
        for i in range(min(5, len(entries))):
            label = FIRST5[i]
            pretty_text = self.pretty_display(npc_idx, i)
            it = TreeEntryItem([f"[{i+1}] {label}: {self.one_line(pretty_text)}"]); it.set_entry_index(i); fixedRoot.addChild(it)

        i = 5
        keywordsRoot = TreeEntryItem(["Keywords"]); keywordsRoot.set_entry_index(None)
        labelsRoot = TreeEntryItem(["Labels"]); labelsRoot.set_entry_index(None)
        miscRoot = TreeEntryItem(["Other"]); miscRoot.set_entry_index(None)

        while i < len(entries):
            txt = entries[i]
            lblnum = is_label_start(txt)
            if lblnum is not None:
                # pretty body for display, while keeping logic based on decoded txt
                pretty_full = self.pretty_display(npc_idx, i, force_label=True)
                pretty_body = self.strip_label_heading_pretty(pretty_full)

                body_after_label = self.strip_label_heading(txt)
                body_clean, had_any_on_label = split_any_marker(body_after_label)

                lblNode = TreeEntryItem([f"Label {lblnum}: {self.one_line(pretty_body)}"]); lblNode.set_entry_index(i); labelsRoot.addChild(lblNode)
                i += 1
                if had_any_on_label and i < len(entries) and is_label_start(entries[i]) is None:
                    trigItem = TreeEntryItem(["Trigger: <Any>"]); trigItem.set_entry_index(i - 1); lblNode.addChild(trigItem)
                    resp_pretty = self.pretty_display(npc_idx, i)
                    resp = entries[i]; goto = is_goto_label(resp); suffix = f"  → Label {goto}" if goto else ""
                    respItem = TreeEntryItem([f"Response: {self.one_line(resp_pretty)}{suffix}"]); respItem.set_entry_index(i); lblNode.addChild(respItem)
                    i += 1
                while i < len(entries) and is_label_start(entries[i]) is None:
                    j = i; triggers: List[Tuple[str,int]] = []
                    while j < len(entries):
                        t_raw = entries[j].strip()
                        if is_or(t_raw): j += 1; continue
                        if is_trigger_in_label(t_raw): triggers.append((t_raw, j)); j += 1; continue
                        break
                    if triggers and j < len(entries) and is_label_start(entries[j]) is None:
                        response_idx = j; response_txt = entries[response_idx]
                        goto = is_goto_label(response_txt); suffix = f"  → Label {goto}" if goto else ""
                        for _, tidx in triggers:
                            trig_pretty = self.pretty_display(npc_idx, tidx)
                            trigItem = TreeEntryItem([f"Trigger: {self.one_line(trig_pretty)}"]); trigItem.set_entry_index(tidx); lblNode.addChild(trigItem)
                        resp_pretty = self.pretty_display(npc_idx, response_idx)
                        respItem = TreeEntryItem([f"Response: {self.one_line(resp_pretty)}{suffix}"]); respItem.set_entry_index(response_idx); lblNode.addChild(respItem)
                        i = response_idx + 1; continue
                    t_raw = entries[i]
                    pre, has_any = split_any_marker(t_raw)
                    if has_any and i + 1 < len(entries) and is_label_start(entries[i+1]) is None:
                        if pre.strip():
                            whole_pretty = self.pretty_display(npc_idx, i)
                            pretty_no_any = re.sub(r'(?:\s*\n)?\s*<\s*Any\s*>\s*$', '', whole_pretty, flags=re.I)
                            lineItem = TreeEntryItem([self.one_line(pretty_no_any)]); lineItem.set_entry_index(i); lblNode.addChild(lineItem)
                        trigItem = TreeEntryItem(["Trigger: <Any>"]); trigItem.set_entry_index(i); lblNode.addChild(trigItem)
                        resp_pretty = self.pretty_display(npc_idx, i+1)
                        resp = entries[i+1]; goto = is_goto_label(resp); suffix = f"  → Label {goto}" if goto else ""
                        respItem = TreeEntryItem([f"Response: {self.one_line(resp_pretty)}{suffix}"]); respItem.set_entry_index(i+1); lblNode.addChild(respItem)
                        i += 2; continue
                    if is_or(t_raw):
                        i += 1; continue
                    line_pretty = self.pretty_display(npc_idx, i)
                    lineItem = TreeEntryItem([self.one_line(line_pretty)]); lineItem.set_entry_index(i); lblNode.addChild(lineItem); i += 1
                continue

            if looks_like_keyword(txt) or is_or(txt):
                group_keywords: List[Tuple[str,int]] = []
                while i < len(entries) and (looks_like_keyword(entries[i]) or is_or(entries[i])):
                    if not is_or(entries[i]): group_keywords.append((entries[i].strip(), i))
                    i += 1
                ans_idx = None
                if i < len(entries) and is_label_start(entries[i]) is None:
                    ans_idx = i; i += 1

                # pretty summary for inline keys
                keys_inline = " | ".join(self.pretty_display(npc_idx, kidx) for _, kidx in group_keywords) if group_keywords else "(none)"
                node = TreeEntryItem([keys_inline]); node.set_entry_index(None)

                for _, kidx in group_keywords:
                    kpretty = self.pretty_display(npc_idx, kidx)
                    ki = TreeEntryItem([f"Keyword: {self.one_line(kpretty)}"]); ki.set_entry_index(kidx); node.addChild(ki)

                if ans_idx is not None:
                    ans_text = entries[ans_idx]; goto = is_goto_label(ans_text); suffix = f"  → Label {goto}" if goto else ""
                    ans_pretty = self.pretty_display(npc_idx, ans_idx)
                    ai = TreeEntryItem([f"Answer: {self.one_line(ans_pretty)}{suffix}"]); ai.set_entry_index(ans_idx); node.addChild(ai)

                keywordsRoot.addChild(node); continue

            mi_pretty = self.pretty_display(npc_idx, i)
            mi = TreeEntryItem([self.one_line(mi_pretty)]); mi.set_entry_index(i); miscRoot.addChild(mi); i += 1

        if keywordsRoot.childCount() > 0: self.tree.addTopLevelItem(keywordsRoot)
        if labelsRoot.childCount() > 0: self.tree.addTopLevelItem(labelsRoot)
        if miscRoot.childCount() > 0: self.tree.addTopLevelItem(miscRoot)
        self.tree.expandAll(); self.tint_nodes()

    def strip_label_heading(self, txt: str) -> str:
        m = re.match(r'^\s*(?:<Any>\s*)?<\s*(?:goto\s+)?label\s+[0-9]+\s*>\s*(.*)$',
                     txt, flags=re.I | re.S)
        return m.group(1) if m else txt

    def one_line(self, s: str, maxlen: int = 120) -> str:
        for code, word in TOKEN_BY_CODE.items():
            s = s.replace(f"<{word}>", " " + word + " ")
        s = re.sub(r" +", " ", s.replace("\n", " / ")).strip()
        return s if len(s) <= maxlen else s[:maxlen-1] + "…"

    def tint_nodes(self):
        def set_color(item: QtWidgets.QTreeWidgetItem, qcolor: QtGui.QColor):
            item.setForeground(0, QtGui.QBrush(qcolor))
        for r in range(self.tree.topLevelItemCount()):
            root = self.tree.topLevelItem(r)
            title = root.text(0).lower()
            if "keyword" in title: set_color(root, QtGui.QColor("#2b7"))
            if "labels" in title: set_color(root, QtGui.QColor("#27f"))
            for i in range(root.childCount()):
                ch = root.child(i); t = ch.text(0).lower()
                if t.startswith("answer:"): set_color(ch, QtGui.QColor("#999"))
                if t.startswith("keyword:"): set_color(ch, QtGui.QColor("#0a8"))
                if t.startswith("trigger:"): set_color(ch, QtGui.QColor("#b70"))
                if t.startswith("response:"): set_color(ch, QtGui.QColor("#999"))

    # Selection & editor
    def on_tree_selection(self):
        if not self.apply_editor_if_dirty():
            return
        self.editor.blockSignals(True); self.editor.clear()
        items = self.tree.selectedItems()
        if not items:
            self._current_edit_idx = None; self.editor.blockSignals(False); return
        item = items[0]
        try: idx = int(item.data(0, TreeEntryItem.EntryIndexRole))
        except Exception: idx = -1
        npc_idx = self.npcList.currentRow()
        self._current_npc_idx = npc_idx
        if idx is None or idx < 0 or npc_idx < 0:
            self._current_edit_idx = None; self.editor.blockSignals(False); return
        self._current_edit_idx = idx

        # Decide which bytes to pretty-decode for the editor view:
        current_text = self.model.decoded[npc_idx][idx]
        baseline = self.model.rendered[npc_idx][idx] if npc_idx < len(self.model.rendered) and idx < len(self.model.rendered[npc_idx]) else None
        if baseline is not None and current_text == baseline:
            raw_bytes = self.model.entries_raw[npc_idx][idx]
        else:
            raw_bytes = encode_entry(current_text)[:-1]

        # >>> NEW: treat true label headers as <Label N> in the editor too
        force_label = is_label_start(current_text) is not None

        pretty = pretty_decode_bytes(raw_bytes, force_label=force_label)
        self.editor.setPlainText(pretty)
        self._editor_dirty = False
        self.editor.blockSignals(False)

    # Insert helpers
    def insert_tag(self, tag: str):
        cur = self.editor.textCursor(); cur.insertText(tag)

    def insert_gold(self):
        n = int(self.goldSpin.value())
        cur = self.editor.textCursor(); cur.insertText(f"<Gold - {n:03d}>")

    def insert_change_item(self):
        n = int(self.itemSpin.value())
        cur = self.editor.textCursor(); cur.insertText(f"<Change><Item: {n}>")

    # Context menu (same behavior as your last working version)
    def on_tree_context_menu(self, pos: QtCore.QPoint):
        item = self.tree.itemAt(pos)
        if item is None:
            return
        idx = item.data(0, TreeEntryItem.EntryIndexRole)
        try:
            entry_idx = int(idx)
        except Exception:
            entry_idx = -1
        npc_idx = self.npcList.currentRow()
        if npc_idx < 0:
            return

        menu = QtWidgets.QMenu(self)
        act_add_keyword_above = menu.addAction("Add Keyword Above")
        act_add_keyword_below = menu.addAction("Add Keyword Below")
        act_add_answer_after = menu.addAction("Add Answer Below")
        menu.addSeparator()
        #act_add_label_here = menu.addAction("Add Label Here")
        act_add_trigger_below = menu.addAction("Add Trigger Below")
        act_add_response_after = menu.addAction("Add Response Below")
        #act_add_or_below = menu.addAction("Add <Or> Below")
        menu.addSeparator()
        act_delete = menu.addAction("Delete Selected")

        chosen = menu.exec(self.tree.viewport().mapToGlobal(pos))
        if chosen is None:
            return

        def insert_entry(at: int, text: str):
            if not self.apply_editor_if_dirty():
                return
            self.model.decoded[npc_idx].insert(at, text)
            self.populate_tree(npc_idx)

        def delete_entry(at: int):
            if not self.apply_editor_if_dirty():
                return
            if 0 <= at < len(self.model.decoded[npc_idx]):
                del self.model.decoded[npc_idx][at]
                self.populate_tree(npc_idx)

        if chosen == act_add_keyword_above:
            if entry_idx < 0: return
            insert_entry(entry_idx, "newkey")
        elif chosen == act_add_keyword_below:
            if entry_idx < 0: return
            insert_entry(entry_idx + 1, "newkey")
        elif chosen == act_add_answer_after:
            if entry_idx < 0: return
            insert_entry(entry_idx + 1, "New answer text")
        elif chosen == act_add_trigger_below:
            if entry_idx < 0: return
            at = entry_idx + 1
            def is_trigger_entry(txt: str) -> bool:
                t = txt.strip()
                if t.lower() == "<or>": return False
                if is_trigger_in_label(t): return True
                if ANY_TAIL_RE.search(t): return True
                return False
            entries_list = self.model.decoded[npc_idx]
            need_or = False
            prev_idx = entry_idx
            if 0 <= prev_idx < len(entries_list) and is_trigger_entry(entries_list[prev_idx]):
                j = prev_idx; saw_any_trigger = False
                while j < len(entries_list):
                    s = entries_list[j].strip()
                    if is_or(s): j += 1; continue
                    if is_trigger_entry(s): saw_any_trigger = True; j += 1; continue
                    break
                if saw_any_trigger: need_or = True
            if need_or:
                insert_entry(at, "<Or>"); at += 1
            insert_entry(at, "newtrigger")
        elif chosen == act_add_response_after:
            if entry_idx < 0: return
            insert_entry(entry_idx + 1, "New response")
        elif chosen == act_delete:
            if entry_idx < 0: return
            delete_entry(entry_idx)

        def next_unused_label(self, npc_idx: int) -> Optional[int]:
            if npc_idx < 0 or npc_idx >= len(self.model.decoded):
                return None
            used: set[int] = set()
            rx = re.compile(r'<\s*(?:goto\s+)?label\s+([1-9]|1[0-5])\s*>', flags=re.I)
            for s in self.model.decoded[npc_idx]:
                for m in rx.finditer(s):
                    used.add(int(m.group(1)))
            for n in range(1, 16):
                if n not in used:
                    return n
            return None

        def insert_new_label_block(self):
            npc_idx = self.npcList.currentRow()
            if npc_idx < 0 or npc_idx >= len(self.model.decoded):
                QtWidgets.QMessageBox.warning(self, "No NPC Selected", "Select an NPC first.")
                return

            if not self.apply_editor_if_dirty():
                return

            n = self.next_unused_label(npc_idx)
            if n is None:
                QtWidgets.QMessageBox.warning(self, "Labels Full", "All label numbers 1–15 are already used.")
                return

            # Minimal working block:
            # 1) Label header with body text
            # 2) Default trigger on that label (<Any>)
            # 3) A response line
            entries = self.model.decoded[npc_idx]
            entries.append(f"<Any><Label {n}>New label text")
            entries.append("<Any>")
            entries.append("New response")

            # Select the response so you can type immediately
            new_resp_idx = len(entries) - 1
            self.populate_tree(npc_idx)
            self.select_tree_entry(new_resp_idx)

# -------------------------

def main():
    app = QtWidgets.QApplication(sys.argv)
    w = MainWindow()
    w.show()
    sys.exit(app.exec())

if __name__ == "__main__":
    main()

