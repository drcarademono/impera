# Ultima 5 Python editors

These tools edit original DOS game resources. Their file layouts are checked
against Impera's decompiled readers; they do not edit modern multi-save
containers or alternative-platform artwork.

Use Python 3.10 or newer. Install the GUI/image dependencies with:

```sh
python -m pip install -r python-editors/requirements.txt
```

| Tool | Purpose |
| --- | --- |
| `u5_map_editor.py` | Britannia, Underworld, settlement and combat maps; settlement NPC schedules, appearances and dialogue IDs |
| `u5_brit_map.py` | Export Britannia as tile IDs, a quick-look image or a render using DOS tiles |
| `u5_init_editor.py` | Starting state in `INIT.GAM`, or the original 4192-byte `SAVED.GAM`: party, position, inventory, time, quest flags and raw bytes |
| `u5_tiles.py` | Convert `TILES.16` to/from a 512×256 EGA PNG sheet |
| `u5_16_editor.py` | Export/import the tileset or individual images in other `.16` resources, including one-bit transparency masks |
| `u5_tlk_gui.py`, `u5_dialogue_editor.py` | Edit NPC dialogue and its conversation control tags |
| `add_tails_tlk.py` | Append conversation tail bytes to a TLK file; repeat runs leave existing tails intact unless `--force` is specified |
| `u5_story_tool.py` | Edit `STORY.DAT` without relocating text used by the intro |

Keep backups of the game directory. In particular, saving Britannia can update
**both `BRIT.DAT` and the chunk-index bytes in `DATA.OVL`**. Keep that pair together.
The map editor stages both files and restores their previous contents on a normal
I/O failure; an abrupt process or machine shutdown between replacements is not
transactional.

Examples, from the repository root:

```sh
python python-editors/u5_map_editor.py --help
python python-editors/u5_brit_map.py --data-ovl '/path/to/Ultima 5/DATA.OVL' --brit '/path/to/Ultima 5/BRIT.DAT' --out brit.png
python python-editors/u5_init_editor.py '/path/to/Ultima 5/INIT.GAM'
python python-editors/u5_16_editor.py
python python-editors/u5_tlk_gui.py
python python-editors/u5_story_tool.py '/path/to/Ultima 5/STORY.DAT'
```

## Format audit fixes

- All graphics/map tools share the same LSB-first LZW codec. Its width changes and
  dictionary resets match `src/common/lzw.c`; raw 65536-byte tilesets are accepted
  without interpreting their first pixels as a compression header.
- Multi-image `.16` files have a pair of **16-bit color and mask offsets** per
  image. Mask rows are byte-padded independently. Alpha edits regenerate masks;
  transparent PNG RGB values need not be EGA colors. Partial alpha is rejected,
  and `TILES.16` cannot store PNG alpha. Unchanged multi-image data retains its
  original padding and bytes; offsets that cannot fit are rejected.
- Britannia edits to implicit-water chunks and shared chunks are saved correctly.
  The editor updates the index instead of silently discarding those edits or
  applying them at other locations. More than 255 distinct non-water chunks is
  unrepresentable and rejected before writing either file.
- Settlement basement numbering matches the engine, including Yew, both castles
  and Serpent's Hold. NPCs appear on the corresponding floor. Settlement trailing
  bytes are retained, and lowercase companion `.npc` files are recognized.
  Combat coordinate controls no longer overlap their next row or clamp raw
  sentinel values when another coordinate is edited. Underworld extension bytes
  are preserved on save.
- INIT refreshes do not modify file bytes or reset other fields. Save As changes
  the subsequent save destination. Party membership is a contiguous prefix of
  character records, as in the engine: adding a member swaps it into that prefix,
  removing one shifts the records and asks for its destination settlement. The
  count is derived from these operations; the Avatar cannot be removed and the
  party cannot exceed six members. Malformed hex input is rejected instead of
  skipping bytes and shifting the rest of the file.
- Both dialogue editors use the engine's word table (including `thee`, `thee,`,
  `these`, `great` and `Great`). Compressed words appear as readable tags such as
  `<the>` so their boundaries survive edits. Existing literal text stays literal.
  Quotes, gold operands, conditional operands, unknown controls and all fifteen
  labels survive round trips. Label definitions retain their `<Any>` marker;
  operands are not mistaken for labels. `<Set Flag>` is actually an **if-met
  branch** and needs an operand; its button inserts `<Unknown: 255>` for the
  engine's default-answer branch. Newlines encode as `<New Line>`, while an
  original literal `@` stays a literal `@`.
- Unchanged dialogue files and unchanged NPC segments retain their original
  bytes/tails/padding. Missing final terminators no longer silently discard text.
  Duplicate NPC IDs, malformed offsets, offset overflow and conversations over
  the engine's 1024-byte buffer are rejected before writing. The tail utility
  recognizes existing tails followed by NUL padding.
- STORY pages use the fixed `D_3016` offsets, not every NUL in padding. Edits stay
  within the original payload, preserve terminators, and reject embedded NUL.
  Reverting an oversized edit uses the correct Qt 6 cursor API.

## Regression tests

```sh
python python-editors/tests/run_tests.py
```

The runner uses separate processes for PySide6 and PyQt6, with Qt's offscreen
platform. Synthetic fixtures exercise file layouts, edited masks, map allocation,
GUI refreshes, party operations and dialogue controls. When `cc` is available,
the suite also compiles the actual engine LZW decoder and reads files produced by
the Python encoder.

Optional original-resource checks use `Ultima 5/` in the repository, or the
`U5_GAME_DIR` environment variable. Those checks are skipped if game resources
are absent; no original game data is included in the test fixtures. Linux
headless GUI checks are automated; interactive Windows/macOS behavior still
needs testing on those platforms.
