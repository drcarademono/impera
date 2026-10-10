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
import sys, struct, re
from typing import List, Tuple, Optional, Dict
from PyQt6 import QtWidgets, QtGui, QtCore

# -------------------------
# Compat token tables (includes "may" fix)
# -------------------------
TOKEN_GROUPS = [
    (1,  ["the","thou","of","to","and","that","for"]),
    (9,  ["in","is","have","with","you","this","not","my","it","me","but","dost","know","be","was",
          "Blackthorn","from","thy","one"]),
    (29, ["are","here","many","Lord","am","we","they","he","would","art","on","young","what","see",
          "like","only","by","there","Blackthorn's","good","been"]),
    (51, ["must","his","British","fine","an","great","thee","our","who","name","heard","as","at","has"]),
    (66, ["through"]),
    (68, ["once","can"]),
    (71, ["him"]),
    (76, ["ye","Shadowlords","tell","some","believe","all","their","upon","even","'tis","find","if","about",
          "don't","before","those","just","make","will","when","three","Great","might","those","old","hast",
          "ask","unto","wish","man","so","knows","still","Mantra","out","help","well","shall","think","where",
          "named","talking","more","such","very","may","lives","canst","which","since","need","I've","work",
          "<insert Avatar's name here>"]),
]

WORDMAP: Dict[str,int] = {}
TOKEN_BY_CODE: Dict[int,str] = {}
for base, lst in TOKEN_GROUPS:
    for i, w in enumerate(lst):
        code = base + i
        WORDMAP[w.lower()] = code
        TOKEN_BY_CODE[code] = w
WORDMAP["<avatar>"] = 129  # convenience

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

TAG_RE_LABEL = re.compile(r'^<\s*(?:goto\s+)?label\s+([1-9]|10)\s*>$', re.I)
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
        out.append(int(m.group(1)) & 0xFF)
        return end + 1

    for rx in (TAG_RE_WORD, TAG_RE_UNK):
        m = rx.match(tag)
        if m:
            out.append(int(m.group(1)) & 0xFF)
            return end + 1

    return -1

def encode_entry(text: str) -> bytes:
    # SPECIAL CASE: label header entries must encode as 0x90 <Any> first, then the label byte (145..155)
    m = re.match(r'^\s*<\s*(?:goto\s+)?label\s+([1-9]|10)\s*>\s*(.*)$', text, flags=re.I | re.S)
    if m and m.group(2).strip() != "":
        n = int(m.group(1))
        body = m.group(2)

        out = bytearray()
        # vanilla order: <Any> (0x90) first, then the label/goto byte (145..155)
        out.append(144)
        out.append(144 + n)

        # encode the body, but skip any <Any> tags the user may have typed
        i = 0
        while i < len(body):
            c = body[i]
            if c == '<':
                end = body.find('>', i)
                if end != -1:
                    tag = body[i:end+1]
                    if tag.strip().lower() == "<any>":
                        i = end + 1
                        continue
                    nxt = try_emit_tag(body, i, out)
                    if nxt != -1:
                        i = nxt
                        continue
            nxt = try_emit_token_word(body, i, out)
            if nxt != -1:
                i = nxt
                continue
            uc = ord(c)
            if 32 <= uc <= 126:
                out.append((uc + 128) & 0xFF)
            i += 1

        out.append(0)
        return bytes(out)

    # ---- default path (unchanged) ----
    out = bytearray()
    i = 0
    while i < len(text):
        c = text[i]
        if c == '<':
            nxt = try_emit_tag(text, i, out)
            if nxt != -1:
                i = nxt
                continue
        nxt = try_emit_token_word(text, i, out)
        if nxt != -1:
            i = nxt
            continue
        uc = ord(c)
        if 32 <= uc <= 126:
            out.append((uc + 128) & 0xFF)
        i += 1
    out.append(0)
    return bytes(out)

def process_byte_decode(c: int, was_phrase: List[bool], defaultanswer: List[bool]) -> str | None:
    if c in (159, 255, 162):
        return ""
    if 160 <= c < 255:
        ch = chr(c - 128)
        if ch == '@':
            return '\n'
        was_phrase[0] = False
        return ch
    if c < 129:
        # return token word without spaces (round-trip safety)
        w = TOKEN_BY_CODE.get(c)
        if w is not None:
            was_phrase[0] = True
            return w
        was_phrase[0] = True
        return f"<WORD:{c}>"
    if c == 129:
        return "<Avatar>"
    if c == 144:
        defaultanswer[0] = True
        return ""
    ctrl = {
        130: "<End Conversation>", 131: "<Pause>", 132: "<Join Party>",
        133: "<Gold - ", 134: "<Change>", 135: "<Or>", 136: "<Ask Name>",
        137: "<Karma + 1>", 138: "<Karma - 1>", 139: "<Call Guards>",
        140: "<Set Flag>", 141: "<New Line>", 142: "<Rune>", 143: "<Key Wait>",
    }.get(c)
    if ctrl is not None:
        was_phrase[0] = False
        return ctrl
    return f"<Unknown: {c}>"

def count_label_occurrences(entries: List[bytes]) -> Dict[int,int]:
    counts: Dict[int,int] = {}
    for e in entries:
        for b in e:
            if 145 <= b <= 155:
                n = b - 144
                counts[n] = counts.get(n, 0) + 1
    return counts

def decode_entry(data: bytes, label_remaining: Dict[int,int]) -> str:
    out: List[str] = []
    was_phrase = [False]
    defaultanswer = [False]
    i = 0
    writegold = 5
    while i < len(data):
        c = data[i]
        if c == 0:
            break
        if 145 <= c <= 155:
            n = c - 144
            rem = label_remaining.get(n, 0)
            if rem > 0:
                rem -= 1
                label_remaining[n] = rem
                tag = f"<Label {n}>" if rem == 0 else f"<Goto Label {n}>"
            else:
                tag = f"<Label {n}>"
            out.append(tag); was_phrase[0] = False; i += 1; continue
        s = process_byte_decode(c, was_phrase, defaultanswer)
        if s is None:
            i += 1; continue
        out.append(s)
        if s == "<Gold - ":
            writegold = 0
        else:
            if writegold < 4:
                writegold += 1
                if writegold == 4:
                    out.append('>')
                    writegold += 1
        if s == "<Change>" and i + 1 < len(data):
            i += 1
            out.append(f"<Item: {data[i]}>")
        i += 1
    if defaultanswer[0]:
        out.append("\n<Any>")
    return "".join(out)

# ---------- Token-aware PRETTY decoder (editor view only) ----------

def pretty_decode_bytes(data: bytes, force_label: bool = False) -> str:
    """Decode bytes like the compat tool, but add human spaces around token words
       and strip space before punctuation. Never splits inside ASCII words.
       Additionally, defer the <Any> marker (0x90) to the end of the entry,
       matching decode_entry()'s normalized form."""
    out: List[str] = []
    last = ''  # last emitted character (for simple look-behind)
    after_token = False
    default_any = False  # seen 0x90 in this entry

    def emit(s: str):
        nonlocal last
        if not s:
            return
        out.append(s)
        last = s[-1]

    def maybe_space_before_next_wordlike():
        nonlocal last
        if last and not last.isspace() and last not in "('":
            emit(' ')

    i = 0
    writegold = 5
    while i < len(data):
        c = data[i]
        if c == 0:
            break

        # labels -> textual tag (no extra spacing)
        if 145 <= c <= 155:
            n = c - 144
            tag = f"<Label {n}>" if force_label else f"<Goto Label {n}>"
            emit(tag)
            after_token = False
            i += 1
            continue

        # compressed word?
        if c < 129 and c in TOKEN_BY_CODE:
            w = TOKEN_BY_CODE[c]
            # leading space if needed
            if out and not out[-1].endswith((' ', '\t', '\n')) and last not in "('":
                emit(' ')
            emit(w)
            after_token = True
            i += 1
            continue

        # swallow odd
        if c in (159, 255, 162):
            i += 1
            continue

        # controls & mapped ASCII
        if 160 <= c < 255:
            ch = chr(c - 128)
            if ch == '@':
                emit('\n'); after_token = False; i += 1; continue
            # punctuation: strip preceding space
            if ch in ".,;:!?')":
                if out and out[-1].endswith(' '):
                    out[-1] = out[-1][:-1]
                emit(ch)
                after_token = False
            else:
                if after_token and ch not in " )]\t\r\n":
                    maybe_space_before_next_wordlike()
                emit(ch)
                after_token = False
            i += 1
            continue

        # specials mapped to tags
        if c == 129:
            if out and not out[-1].endswith((' ', '\t', '\n')) and last not in "('":
                emit(' ')
            emit("<Avatar>")
            after_token = True
            i += 1
            continue
        if c == 144:
            # Defer <Any> to the end, don't emit inline
            default_any = True
            after_token = False
            i += 1
            continue

        ctrl = {
            130: "<End Conversation>",
            131: "<Pause>",
            132: "<Join Party>",
            133: "<Gold - ",
            134: "<Change>",
            135: "<Or>",
            136: "<Ask Name>",
            137: "<Karma + 1>",
            138: "<Karma - 1>",
            139: "<Call Guards>",
            140: "<Set Flag>",
            141: "<New Line>",
            142: "<Rune>",
            143: "<Key Wait>",
        }.get(c)
        if ctrl is not None:
            if out and out[-1].endswith(' ') and not ctrl.startswith("<New Line>"):
                out[-1] = out[-1][:-1]
            emit(ctrl)
            if ctrl == "<Gold - ":
                writegold = 0
            else:
                if writegold < 4:
                    writegold += 1
                    if writegold == 4:
                        emit('>')
                        writegold += 1
            after_token = False
            i += 1
            if ctrl == "<Change>" and i < len(data):
                emit(f"<Item: {data[i]}>")
                i += 1
            continue

        emit(f"<Unknown: {c}>")
        after_token = False
        i += 1

    pretty = "".join(out)
    if default_any:
        # Append trailing <Any> if not already present at the end
        if not re.search(r'(?:\s*\n)?\s*<\s*Any\s*>\s*$', pretty, flags=re.I):
            if pretty and not pretty.endswith('\n'):
                pretty += '\n'
            pretty += "<Any>"
    return pretty

# -------------------------
# TLK I/O
# -------------------------

def read_tlk(path: str) -> Tuple[List[Tuple[int,int]], bytes]:
    with open(path, 'rb') as f:
        d = f.read()
    if len(d) < 2:
        raise ValueError("TLK too small")
    count = struct.unpack_from('<H', d, 0)[0]
    need = 2 + count * 4
    if len(d) < need:
        raise ValueError("TLK header truncated")
    headers: List[Tuple[int,int]] = []
    p = 2
    for _ in range(count):
        npc_id, off = struct.unpack_from('<HH', d, p)
        p += 4
        headers.append((npc_id, off))
    return headers, d

def extract_npc_entries(headers, blob: bytes) -> List[Tuple[int, List[bytes]]]:
    entries_all: List[Tuple[int, List[bytes]]] = []
    for i, (npc_id, off) in enumerate(headers):
        start = off
        end = headers[i+1][1] if i+1 < len(headers) else len(blob)
        segment = bytearray(blob[start:end])
        one: List[bytes] = []
        cur = bytearray()
        for b in segment:
            if b == 0:
                one.append(bytes(cur)); cur.clear()
            else:
                cur.append(b)
        entries_all.append((npc_id, one))
    return entries_all

def write_tlk(path: str, npc_entries: List[Tuple[int, List[bytes]]]) -> None:
    count = len(npc_entries)
    header_size = 2 + count * 4
    cur_off = header_size
    out = bytearray()
    out += struct.pack('<H', count)
    hdr_bytes = bytearray()
    script = bytearray()
    offsets = []
    for npc_id, items in npc_entries:
        offsets.append((npc_id, cur_off))
        for ent in items:
            script += ent
            script.append(0)
            cur_off += len(ent) + 1
    for npc_id, off in offsets:
        hdr_bytes += struct.pack('<HH', npc_id, off)
    out += hdr_bytes
    out += script
    with open(path, 'wb') as f:
        f.write(out)

def write_tlk_with_tails(path: str,
                         npc_entries: List[Tuple[int, List[bytes]]],
                         tail: bytes = b"\x90\x9F\xC0") -> None:
    """
    Build a TLK where each NPC segment is:
      entry + 0x00 + entry + 0x00 + ... + 0x00 + TAIL
    (ensuring there's a 0x00 separator before the tail, like the CLI tool.)
    """
    count = len(npc_entries)

    # Build segments (entries + 0x00, then tail)
    segs: List[Tuple[int, bytes]] = []
    for npc_id, items in npc_entries:
        seg = bytearray()
        for ent in items:
            seg += ent
            seg.append(0)  # separator after every entry (incl. last)
        # Append the tail exactly once per segment, like the CLI
        if tail and not seg.endswith(tail):
            seg += tail
        segs.append((npc_id, bytes(seg)))

    # Header with recomputed offsets
    header_size = 2 + count * 4
    cur_off = header_size
    out = bytearray()
    out += struct.pack('<H', count)
    for npc_id, seg in segs:
        out += struct.pack('<HH', npc_id, cur_off)
        cur_off += len(seg)

    # Body
    for _npc_id, seg in segs:
        out += seg

    with open(path, 'wb') as f:
        f.write(out)


# -------------------------
# Conversation structure parser (for the tree)
# -------------------------

FIRST5 = ["Name", "Description", "Greeting", "Job", "Bye"]

def is_label_start(txt: str) -> Optional[int]:
    # Consider both <Label N> and <Goto Label N> as a "label header" IFF
    # there is body text after the tag. Pure "<Goto Label N>" lines are jumps.
    m = re.match(r'^\s*<\s*(?:goto\s+)?label\s+([1-9]|10)\s*>\s*(.*)$',
                 txt, flags=re.I | re.S)
    if not m:
        return None
    body = m.group(2)
    return int(m.group(1)) if body.strip() != "" else None

def is_goto_label(txt: str) -> Optional[int]:
    m = re.search(r'<\s*goto\s+label\s+([1-9]|10)\s*>', txt, flags=re.I)
    if m: return int(m.group(1))
    return None

def is_or(txt: str) -> bool:
    return txt.strip().lower() == "<or>"

def looks_like_keyword(txt: str) -> bool:
    s = txt.strip()
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
        self.path = path
        headers, blob = read_tlk(path)
        self.headers = headers
        self.npcs = extract_npc_entries(headers, blob)
        self.entries_raw = [entries[:] for _, entries in self.npcs]

        self.decoded = []
        for _npc_id, raw_entries in self.npcs:
            remaining = count_label_occurrences(raw_entries)
            texts = [decode_entry(e, remaining) for e in raw_entries]
            self.decoded.append(texts)

        # keep a baseline copy for unchanged detection
        self.rendered = []
        for _npc_id, raw_entries in self.npcs:
            remaining2 = count_label_occurrences(raw_entries)
            texts2 = [decode_entry(e, remaining2) for e in raw_entries]
            self.rendered.append(texts2)

        self.dataChanged.emit()

    def save_as(self, path: str):
        # commit any pending edit before saving
        self.parent().apply_editor_if_dirty()

        new_entries: List[Tuple[int, List[bytes]]] = []
        for npc_idx, ((npc_id, _raw), texts) in enumerate(zip(self.npcs, self.decoded)):
            packed: List[bytes] = []
            for ent_idx, txt in enumerate(texts):
                # Reuse unchanged raw bytes exactly; otherwise re-encode (omit trailing 0x00)
                if npc_idx < len(self.rendered) and ent_idx < len(self.rendered[npc_idx]) \
                   and txt == self.rendered[npc_idx][ent_idx]:
                    packed.append(self.entries_raw[npc_idx][ent_idx])
                else:
                    packed.append(encode_entry(txt)[:-1])
            new_entries.append((npc_id, packed))

        # Write with per-NPC tail (same behavior as the CLI that worked)
        write_tlk_with_tails(path, new_entries, tail=b"\x90\x9F\xC0")
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
            b.clicked.connect(lambda _, t=tag: self.insert_tag(t))
            tagLayout.addWidget(b)

        goldItemLayout = QtWidgets.QHBoxLayout()
        self.goldSpin = QtWidgets.QSpinBox(); self.goldSpin.setRange(0, 999); self.goldSpin.setValue(3)
        self.itemSpin = QtWidgets.QSpinBox(); self.itemSpin.setRange(0, 255); self.itemSpin.setValue(65)
        bGold = QtWidgets.QPushButton("Insert <Gold - ###>"); bGold.clicked.connect(self.insert_gold)
        bChange = QtWidgets.QPushButton("Insert <Change><Item:#>"); bChange.clicked.connect(self.insert_change_item)
        goldItemLayout.addWidget(QtWidgets.QLabel("Gold:")); goldItemLayout.addWidget(self.goldSpin); goldItemLayout.addWidget(bGold)
        goldItemLayout.addWidget(QtWidgets.QLabel("Item:")); goldItemLayout.addWidget(self.itemSpin); goldItemLayout.addWidget(bChange)

        labelLayout = QtWidgets.QHBoxLayout()
        self.labelSpin = QtWidgets.QSpinBox(); self.labelSpin.setRange(1, 10); self.labelSpin.setValue(1)
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
        rx = re.compile(r'<\s*(?:goto\s+)?label\s+([1-9]|10)\s*>', flags=re.I)
        for s in self.model.decoded[npc_idx]:
            for m in rx.finditer(s):
                used.add(int(m.group(1)))
        for n in range(1, 11):
            if n not in used:
                return n
        return None

    def insert_new_label_entry(self):
        npc_idx = self.npcList.currentRow()
        if npc_idx < 0 or npc_idx >= len(self.model.decoded):
            QtWidgets.QMessageBox.warning(self, "No NPC Selected", "Select an NPC first.")
            return

        self.apply_editor_if_dirty()

        n = self.next_unused_label(npc_idx)
        if n is None:
            QtWidgets.QMessageBox.warning(self, "Labels Full", "All label numbers 1–10 are already used.")
            return

        # IMPORTANT: include a placeholder body so is_label_start() treats it as a header
        placeholder = "New label text"
        self.model.decoded[npc_idx].append(f"<Label {n}>{placeholder}")
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
            self.apply_editor_if_dirty(); self.model.load(path)
        except Exception as e:
            QtWidgets.QMessageBox.critical(self, "Error", str(e))

    def save_as(self):
        if not self.model.decoded: return
        path, _ = QtWidgets.QFileDialog.getSaveFileName(self, "Save TLK As", "", "TLK Files (*.TLK *.tlk);;All Files (*)")
        if not path: return
        try:
            self.apply_editor_if_dirty(); self.model.save_as(path)
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
        raw = raw.splitlines()[0].strip()
        return raw

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
        """Commit editor text to the selected entry (store as-is; encoder ensures bytes on save)."""
        if not self._editor_dirty: return
        if self._current_npc_idx is None or self._current_edit_idx is None:
            self._editor_dirty = False; return
        if self._current_npc_idx < 0 or self._current_edit_idx < 0:
            self._editor_dirty = False; return
        try:
            self.model.decoded[self._current_npc_idx][self._current_edit_idx] = self.editor.toPlainText()
        except Exception:
            pass
        self._editor_dirty = False
        self.populate_tree(self._current_npc_idx)

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
        m = re.match(r'^\s*<\s*(?:goto\s+)?label\s+[0-9]+\s*>\s*(.*)$', pretty, flags=re.I|re.S)
        return m.group(1) if m else pretty

    # Tree building
    def on_npc_changed(self, npc_idx: int):
        self.apply_editor_if_dirty()
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
        m = re.match(r'^\s*<\s*(?:goto\s+)?label\s+[0-9]+\s*>\s*(.*)$',
                     txt, flags=re.I | re.S)
        return m.group(1) if m else txt

    def one_line(self, s: str, maxlen: int = 120) -> str:
        s = s.replace("\n", " / ").strip()
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
        self.apply_editor_if_dirty()
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
            self.apply_editor_if_dirty()
            self.model.decoded[npc_idx].insert(at, text)
            self.populate_tree(npc_idx)

        def delete_entry(at: int):
            self.apply_editor_if_dirty()
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
            rx = re.compile(r'<\s*(?:goto\s+)?label\s+([1-9]|10)\s*>', flags=re.I)
            for s in self.model.decoded[npc_idx]:
                for m in rx.finditer(s):
                    used.add(int(m.group(1)))
            for n in range(1, 11):
                if n not in used:
                    return n
            return None

        def insert_new_label_block(self):
            npc_idx = self.npcList.currentRow()
            if npc_idx < 0 or npc_idx >= len(self.model.decoded):
                QtWidgets.QMessageBox.warning(self, "No NPC Selected", "Select an NPC first.")
                return

            self.apply_editor_if_dirty()

            n = self.next_unused_label(npc_idx)
            if n is None:
                QtWidgets.QMessageBox.warning(self, "Labels Full", "All label numbers 1–10 are already used.")
                return

            # Minimal working block:
            # 1) Label header with body text
            # 2) Default trigger on that label (<Any>)
            # 3) A response line
            entries = self.model.decoded[npc_idx]
            entries.append(f"<Label {n}>New label text")
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

