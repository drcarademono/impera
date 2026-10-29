# City design for Ultima IX: The Vacant Throne

An AI-directed editing toolkit for **existing Impera Workshop JSON**, not a new map
format or a random city generator. Work on `ultima-ix` task branches and submit PRs
to `ultima-ix`. No engine or Workshop source modifications are required.

The first study is [Minoc](experiments/minoc/REPORT.md). Its final importable file is
[study/06-final.json](experiments/minoc/study/06-final.json). The companion [Minoc-experiment.imperamod](experiments/minoc/study/Minoc-experiment.imperamod)
sets the relocated bottom-center arrival using the existing map catalogue. Import
that package into a separate Workshop project to retain the correct arrival; a
location JSON transfer alone cannot carry portal arrival settings. The original is
[`maps/Minoc.json`](../../maps/Minoc.json), preserved byte-for-byte.

## Current architecture and furnishing revision

Read the [Ultima V style guide](STYLE_GUIDE.md), based on all 32 original locations
and a complete DOS artwork atlas. The [latest Minoc revision](experiments/minoc_furnished/REPORT.md)
rebuilds the central factory as a hall with eight connected rooms, repairs corner
joints, adds purpose-specific furniture, paves main routes, keeps dirt dormitory
spurs and removes desert from the shoreline. Its outputs are separate from the
previous layouts. The native probe also supports read-only `--survey-game` exports;
`style_survey.py` produces the persistent evidence.

## Previous reference study

The new [Ultima VII reference study](experiments/minoc_u7/REPORT.md) starts from
fresh 64×64 terrain, with no town wall, a southwest shipbuilder/bay, central and
southeast factories, a connected river and four rough-walled workers' dormitories.
Its [final JSON](experiments/minoc_u7/study/04-final.json) and
[companion mod](experiments/minoc_u7/study/Minoc-U7-layout.imperamod) are separate
from the earlier preserved-town experiment above. The latest user instruction
explicitly permits replacing terrain for this fresh study; NPC/quest metadata is
still preserved. See its child AGENTS.md and report for that scope.

## Setup and commands

Python 3.10+ and Pillow 10–12 are required. From the repository root:

```sh
python3 -m pip install -r tools/city_design/requirements.txt
python3 -m tools.city_design --help
python3 -m tools.city_design inspect maps/Minoc.json
python3 -m tools.city_design validate maps/Minoc.json
python3 -m tools.city_design render maps/Minoc.json /tmp/minoc-ground.png --scale 2
python3 -m tools.city_design render maps/Minoc.json /tmp/minoc-upper.png --floor 1
python3 -m tools.city_design resize maps/Minoc.json /tmp/minoc-expanded.json --size 64 64 --all-floors
python3 -m tools.city_design paint /tmp/minoc-expanded.json /tmp/minoc-road.json \
  --baseline maps/Minoc.json --region 15 32 2 7 --tile 68
python3 -m tools.city_design building /tmp/minoc-road.json /tmp/minoc-house.json \
  --baseline maps/Minoc.json --pattern house --at 40 40 --size 8 7
python3 -m tools.city_design render /tmp/minoc-house.json /tmp/minoc-detail.png \
  --region 38 38 14 12 --scale 3
python3 -m tools.city_design compare maps/Minoc.json /tmp/comparison.png \
  --after /tmp/minoc-house.json
python3 -m tools.city_design analyze /tmp/minoc-house.json --start 15 31 --target 43 43
python3 -m tools.city_design validate /tmp/minoc-house.json --baseline maps/Minoc.json
python3 -m unittest discover -s tools/city_design/tests -v
```

All output paths must be new. There is deliberately no `--force` switch. CLI edits
protect the authoritative baseline's original rectangle on every floor; a baseline
is required for editing an expanded revision. Use separate output names per step.
For general maps, use the Python API to designate different protected regions when
authorized. Inputs are copied; raw schedules, Base64 data, unknown fields and floor
order are retained. Painting is atomic. Cropping validates metadata coordinates and
fails rather than relocating entities. Explicit user-authorized changes can be recorded with `authorize_terrain` for exact
cells and `authorize_metadata` for an exact replacement value. `validate --baseline`
can apply the hash-bound study manifest with `--authorization`; never invent
authorizations. Minoc permits only the documented wall/gateway cells and two new
gate records. Adding vanilla terrain expands the artwork
slot mask so Workshop imports its exact source artwork.

## Compose intentional designs

```python
from tools.city_design import CityMap
from tools.city_design.patterns import workshop
from tools.city_design.art import render

city = CityMap.load('maps/Minoc.json')
city.protect_original()
for z in (0, 1):
    city.resize(z, 64, 64, 5)
city.stamp(0, 40, 18, workshop(14, 12, wing=(0, 0, 4, 3), door=(7, 11)))
city.paint(0, [(50, 23, 0x58)])  # Explicit verified anvil, not an invented forge ID.
city.save('/tmp/new-experiment.json')
render(city, region=(38, 16, 20, 18), scale=3).save('/tmp/new-experiment.png')
```

The API offers individual-cell edits, rectangle fills, transparent/irregular stamps,
and house, shop, workshop and inn patterns. Patterns are starting points: place rooms,
workstations, storage and circulation for the particular design. Python recipes keep
decisions reviewable without introducing a second tile-map format. Raw data is public
for inspection, but direct mutation is discouraged; export still checks metadata and
registered protected terrain. Register protections before editing.

## Verified artwork and rules

`tile_catalogue.json` covers all 256 DOS terrain slots. Descriptions/categories come
from the complete audited inventory in `docs/workshop-tile-catalog-audit.md`, with
source SHA-256s. Architectural roles are derived from those labels. Party walking
bits come from `src/mod/type_defaults.h` and `TYPES_Walkable` for party shape `0x1c`.
The real native probe checks all 256 bits independently.

The DOS decoder matches `U5::decompress` and `U5::readGraphics` in
`workshop/formats.cpp`: little-endian size header, LSB-first 9–12-bit LZW,
512 sequential 128-byte tiles, high/low pixel nibbles and the exact EGA palette.
Uncompressed 65,536-byte resources also work. `artwork.dos` from the export is
authoritative. `render` draws 16×16 tiles; enlargement uses nearest neighbors.
It supports floors, selected rectangles, and side-by-side change comparisons.

These are accurate **static DOS terrain** views. Actors, animated effects, lighting,
transparency/ground inference and runtime settings are not simulated. Original white
rendering-fragment slots can therefore appear white, exactly as in Workshop's raw
DOS images. Alternate PNG sheets are retained but not selected for rendering. Used
custom artwork, including artwork on vanilla overrides, fails explicitly. No fallback
image, placeholder tile or guessed custom-art decoding is used. Dungeon feature bytes
are validated structurally but rendering and routing require Workshop.

`analyze` reports static four-neighbor reachability, unknown movement tiles, hazardous
tiles, walkable cells outside the start component, blocked door approaches and ladder
pairing. Interior targets distinguish direct reachability from conditional door routes.
Directional stairs produce a warning, because `TOWN_052e` depends on approach direction.
Movement overrides are respected for party profile zero. Boats, other NPC movement
profiles, dynamic gates, locks, schedules, actors and quest scripts are not simulated.

The JSON validator checks the map envelope, grids, ranges, schedules, signs, contents,
gates, world/combat/dungeon structural fields, record lengths and referenced custom IDs.
It is **not** a complete reimplementation of `TYPES_Validate`, alternate-sheet validation,
destination project compatibility, bank allocation or game-wide validation. Native
import validation remains authoritative. NPC schedule locations are intentionally
checked more strictly than the importer to reject edits that strand existing NPCs.

## Real Workshop integration (optional native dependencies)

The test harness calls the actual `Workshop::importMapJson`, `Project::validate` and
`exportMapJson` APIs. It keeps the project in memory and writes only into a new output
directory. It also renders through C++ `U5::readGraphics` and records native walking
rules. It does not drive the GUI or run the game.

Build with Qt 6 Widgets and the repository's C/C++ prerequisites:

```sh
cmake -S tools/city_design/workshop_probe -B build/city-workshop-probe
cmake --build build/city-workshop-probe --target city_workshop_probe --parallel 3
```

To reuse a **freshly rebuilt standalone** Workshop static library and resources on
Linux, first `cmake --build workshop-build --target impera_workshop_core --parallel 3`,
then configure with `-DIMPERA_WORKSHOP_BUILD="$PWD/workshop-build"`. Omit that option
on platforms/builds that do not use the documented `.a`/`.o` layout. In this cloud
environment, first source `/workspace/.impera-setup/env.sh`, and add
`-DCMAKE_PREFIX_PATH=/workspace/.impera-sysroot/usr`
and `-DCMAKE_CXX_FLAGS=-I/workspace/.impera-sysroot/usr/include` when configuring.

```sh
python3 -m tools.city_design.verify_workshop \
  --probe build/city-workshop-probe/city_workshop_probe --game 'Ultima 5' \
  --source maps/Minoc.json --study tools/city_design/experiments/minoc/study \
  --output /tmp/new-minoc-native-check
```

The probe currently resolves built-in **location** targets by their exported name.
World, dungeon, combat and custom-location importer probes are outside this first
city study. Full original game assets and Qt are required; the Python tests only use
synthetic fixtures and the user-supplied Minoc export. No game asset downloads occur.
Importer normalization (for example conditional contents refill encoding and dialogue
allocation) is compared against importing the **same original export**, not mistaken
for an edit made by the city toolkit. Pixel comparison covers both floors and each
experimental revision, not merely a successful process exit.

Native road tiles `0x20–26` are directional narrow paths, not seamless dirt fill.
Use `paths.dirt_path(city, floor, points)` for a one-cell path with matched
straight/corner/junction artwork. Native T junctions use the four-way tile and
retain a spare arm; inspect that visually. For broader streets use continuous
brick/stone paving. Minoc’s final revision uses the original town’s brick paving.

The current final Minoc check includes the authorized wall/gate exceptions:

```sh
python3 -m tools.city_design validate tools/city_design/experiments/minoc/study/06-final.json \
  --baseline maps/Minoc.json \
  --authorization tools/city_design/experiments/minoc/study/validation.json
```

The native integration reads the final arrival from the study manifest, sets that
portal arrival through Workshop’s existing `MAPS.OVR` catalogue, packages it,
imports the package into another private project, and checks the arrival survives.
The committed companion mod contains the final map and this arrival; the original
overworld entrance remains at `(159,20)` on Britannia. This changes Minoc entry,
not the global new-game party start. No engine change is needed.
