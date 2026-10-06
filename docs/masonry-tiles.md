# Darkness fade barriers

Audited against all 256 map tile images in the original `TILES.16`, the map
object names in `LOOK2.DAT`, and the engine's tile definitions. Tile IDs are
hexadecimal map IDs, before sprite animation lookup. No game artwork is bundled
with this audit.

The masonry category keeps visible structural tiles crisp and prevents the
fade from unseen terrain passing through them onto visible ground. Hidden tiles
remain black. This category does not change movement blocking or the original
line-of-sight rules; a window or open arch can still permit sight.

| Map IDs | Original description / reason |
| --- | --- |
| `4a` | Arrow slit, with masonry frame. |
| `4b` | Window, with masonry frame. |
| `4d` | Stone wall: the rough diagonal stone pattern. |
| `4e` | Wall with a nick (concealed door). |
| `4f` | Plain brick wall. |
| `50–57` | All eight crenellation segments, including corners. |
| `5a` | Window shelf, a framed architectural opening. |
| `70–7f` | All sixteen “strange walls” segments. |
| `87` | Archway. |
| `97–98` | Both “odd door” variants. |
| `b8–bb` | Wooden doors, locked doors, and both windowed door variants. |
| `bc` | Fireplace, with a masonry surround. |
| `fe` | Special wall tile. |

There are 39 included map IDs. Tests check every included ID both visible and
hidden, and check that wall barriers prevent fading onto their inside edge.

Excluded groups:

- Natural terrain, trees, mountains, and `4c` (pile of rocks): retain outdoor fading.
- `27–28` roofs, stone floors, `8c` loose floor brick, and `c4–c7` stairs:
  ground/roof features rather than vertical wall segments.
- `46` pillar, Guardian tiles, statues, furniture, bookshelves, manacles,
  metal grates, and freestanding objects: not a continuous masonry boundary.
- `99` portcullis and `ca–cb` wooden fences: metal/wood cutout structures.
- `b0–b1` torches: artwork has a floor background, not a masonry surround.
- `c0–c1`: unnamed graphics, not wall-torch map IDs; removed from the earlier
  incorrectly labelled list.
- `ff`: darkness, not a physical wall.

The implementation is `SolidMasonry` in `src/graphics/widescreen.c`.
