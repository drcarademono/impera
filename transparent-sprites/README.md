Place edited **16×16 RGBA PNG** sprite frames here, retaining the extractor's
filenames (`tile-100.png` through `tile-1ff.png`). Enable them with
`bash scripts/run-linux.sh --transparent-sprites`.

Export the original graphics from the repository root:

```sh
python3 scripts/extract-graphics.py
```

Edit files from `extracted-graphics/tiles-16/sprites/` in GIMP, add an alpha
channel, erase background pixels, and export your finished PNGs into this
folder. Preserve the original pixel dimensions and palette. Edit every animation
and pose frame you want to replace, including sleeping and sitting frames.
The extractor preserves original opaque backgrounds; it does not guess which
pixels should be transparent. Black pixels remain opaque until you erase them.

The export also includes terrain, CGA graphics, menus, portraits, monochrome
images, and fonts, with contact sheets and a JSON manifest. Numeric filenames
identify original resource indices (tile and glyph indices are hexadecimal).
Use `--data-dir PATH` and `--output-dir PATH` to choose locations. Existing
exports are protected unless you explicitly pass `--overwrite`.

Only actor frames (indices 100–1ff) are overridden. Missing, unreadable, or
incorrectly sized overrides fall back to the original game graphics. Filenames
and directory lookup are case insensitive. Overrides support transparent and
partially transparent pixels; normal rendering blends into the original EGA
palette. They also work with fullscreen and smooth movement. Run with
`U5D_DEBUG=1` to see loading diagnostics. The launcher copies these PNGs into
the runtime directory each time it starts; direct executable launches should
place this folder in the executable's working directory.
