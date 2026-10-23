# Extended maps: implementation design

Status: implemented; verification covers the agreed 1–256 tiles on each axis.
Work branch: `feature/extended-maps`.

## Requested behavior

- Resize world, location and combat maps independently by width and height.
  Rectangles must work; dimensions are not a shared square-size setting.
- Create new enterable locations on Britannia or the Underworld, without
  replacing one of the original 32 locations.
- Export, play, save and reload these maps in Impera, including their entities.

## Size decision

The current coordinate format supports 0–255. Dimensions of 1–256 on each axis
can retain those coordinates; dimensions above 256 require a coordinated runtime
and save-format extension. The map-data extension stores width and height explicitly instead of inferring
them from native storage layouts. Coordinates remain bytes for this version.
The chosen range is 1–256 tiles on each axis. Existing byte coordinates remain valid.

This is not just a rendering limit. Relevant implementation points:

- `workshop/formats.h`: `MapPage.side` represents both dimensions.
- `workshop/map_editor.cpp`: map reads, writes and edits assume square terrain.
  The canvas already supports a separate row count, but its surrounding document
  and entity code still assume native dimensions.
- `src/4000.c`: `ULTIMA_4402_BaseAddr` indexes 32×32 location and 11×11 combat
  buffers, or the world's resident four 16×16 chunks.
- `src/graphics/widescreen.c`: world wrapping, location bounds and combat bounds
  repeat the native sizes.
- `src/vars.h`, `src/structs.h`, `src/savegame.c`: party, actor and schedule
  coordinates use bytes, as does the current map identity in party saves.
- `src/mod/types.c`: placement keys encode byte coordinates and map IDs.
  Containers, signs, world objects and entrance tables have related constraints.
- `src/npc.c`, `src/town.c`: loading and exits index the original four banks and
  32-location tables. Increasing just a loop bound would index unrelated data.
- `src/6000.c` and combat consumers: movement, spawning and drawing contain
  fixed 11×11 bounds in addition to the terrain storage.

## Shared map model

Introduce an optional versioned map catalog and terrain override resource.
Each map has a stable identity, kind, width, height, floor identity, background,
name and terrain. Existing maps retain stable references to their native
resource/page; overrides do not rewrite native bank offsets or adjacent floors.
New locations receive independent identities, not reserved vanilla slots.

Use explicit encodings and checked cell counts. Preserve vanilla decoding when
no extension is present. Gate packages containing extended maps so older engines
reject them rather than loading an incorrectly sized board.

Centralize dimensions, boundary behavior and tile access. World wrapping must
use actual width/height independently. Locations and combat boards use actual
edges; their outside background must not alias a tile in the map. Rendering,
selection, painting, clipboard operations, exports, custom tiles, movement and
pathfinding must use this shared geometry.

## Workshop controls

Add a compact map-header action opening Width and Height controls. Preserve
existing cells at their coordinates when resizing; fill newly added cells with
a chosen terrain tile. Preview cropped terrain before applying. Keep resize and
its terrain changes in one undo operation.

Before shrinking, detect NPC schedule destinations, signs, containers, objects,
party starts, entrances, encounter positions and triggers outside the proposed
bounds. Report the affected entries and keep the original map intact until the
user moves or removes them. Do not silently clamp or discard entities.

The tile picker provides Locations, NPCs and Monsters filters alongside terrain
usage filters. Select an entrance tile and place it with the Pencil to supply
the location name, dimensions and initial terrain. Keep the world map open;
open the new location from the Locations list. NPC entries collect a name and
conversation when placed; monster entries place their selected type directly.
These entries are no longer duplicated in Add. The right pane edits entrance,
arrival position and return world; Select / Move drags those markers.

New locations appear in the existing Locations list, sorted by their display
names. Their NPCs and conversations join the existing Dialogue workspace.
Creating or undoing a location must update the catalog, terrain, entrance and
associated entity/conversation resources atomically.

## Runtime and persistence

Separate location identity and map kind from numeric ranges. Vanilla IDs keep
their meaning, while new locations must not accidentally run a vanilla town's
quest logic or share its NPC death/introduction flags.

Load extended terrain through the same tile-access interface used for normal
maps. Integrate entrance detection, arrival and exits with both world maps.
Remember the actual return world and coordinates; an Underworld entrance must
not return the party to Britannia.

Persist extended map identity, coordinates, modified terrain, actor state,
containers and return position in versioned save data. Keep old saves readable.
Resize combat spawning, movement/pathfinding bounds and camera behavior with the
board; make party starts, monster positions and triggers obey its dimensions.

## Required verification

- Rectangular grow/shrink, unchanged neighboring maps, fill, painting, clipboard,
  exports, undo/redo, project save/load and mod export/import.
- Resize rejection for each out-of-bounds entity type, without partial changes.
- Walking and rendering at every edge, independent world wrapping and combat
  movement/spawning/camera behavior on non-native rectangles.
- Enter a genuinely new location from each world, leave, re-enter, save and
  reload inside it. Verify NPC dialogue/state, contents and return positions.
- Existing locations and vanilla quests remain isolated from new identities.
- Exercise coordinate 255 for players, NPCs, monsters, signs, custom tiles and
  containers, then save/reload them. An editor-only large map is not sufficient.

## Implementation

The shared catalog, rectangular documents, canvas/minimap/image exports, resize
control and tile-picker location placement are implemented. New location companions join
the normal map, Dialogue, sign and container editors. The Positions pane links
entrances to arrivals and supports moving entrances between worlds. Undo/redo
includes new map data, companion resources and entrance terrain atomically.

Impera loads the same catalog for terrain, geometry, world wrapping, actor bounds,
NPC schedules/pathfinding, combat movement/spawns/camera, entrance routing and
save state. Original banks and metadata retain their layout. Version 18 packages
gate extended data; saves keep backward compatibility through optional trailers.
See [the package specification](mod-packages.md#extended-maps-and-locations-version-18)
for the binary encodings and identity allocation.

Regression coverage includes coordinate 255, rectangular painting/exports,
unsafe-crop rejection, new-location entity editing and package/project
roundtrips, UI resize and creation with undo/redo, world wrapping, entry/exit on
both worlds, first-load NPCs, combat terrain/metadata separation, camera bounds,
NPC flags, and checksummed terrain persistence.
