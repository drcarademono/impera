# Map placement and starting-state audit

The native Workshop was missing controls that existed in the Python starting-state
editor. In particular, an empty field list hid the general starting settings, and
party position, moon phases, active-member selection, quest items and shared
inventory were not exposed consistently.

## Map positions

Use **Positions** in the map editor. Select a named position in the right pane,
then drag its marker or use **Place on this map** and click a tile.

- **Party start** updates the starting map, floor, coordinates and resident party
  actor's coordinates together. It supports Britannia, Underworld, settlement
  floors and dungeon levels. Game Rules has **Show party start on map**.
- **Moonstones** edit the eight buried-stone positions in the starting state.
  The pane can also put a stone in the starting inventory. **Add… → Moonstone**
  asks for its phase and places that existing stone, rather than creating a ninth
  independent stone. Game Rules has **Show moonstones on map**.
- **Location entrances** edit both parts of an entrance coordinate in one
  operation on Britannia. Settlement names come from the map catalog, including
  castles and dwellings previously labeled as unused in the shop table labels.
  These records control the world entrance/exit position; they are distinct from
  party coordinates inside a settlement. **Move landmark tile** is on by default: relocating the entrance also moves
  its landmark and fills the old tile from nearby ground, in the same undo
  operation. Turn it off to edit only the coordinate. An occupied landmark or
  entrance rejects the move before any resource changes.

The separate row/column entrance tables and moonstone coordinate fields are no
longer exposed in Game Rules. Shop prices and stock remain there. Position edits
use the existing INIT.GAM and RULES.TBL package paths and global undo; they do not
modify original files. They preserve other positions and unused bytes. Party starts and moonstones leave
terrain unchanged; entrance landmark movement changes only the affected tiles.
Starting-state changes affect new games; existing saves retain their state.

## Add menu coverage

The menu is alphabetical and varies with the map and its available records.

| Map item | Representation and editing |
| --- | --- |
| NPC | Settlement schedule, appearance and optional conversation |
| Monster | Settlement NPC slot, world object or combat encounter slot |
| Sign | Native sign record and sign terrain tile on Britannia, Underworld or settlement floors |
| Chest | Explicit mod contents with a visible chest |
| Container | Explicit contents attached to scenery such as a barrel or tree |
| Gold, Food, Gem, Key, Skeleton key, Torch | Native world pickups; quantity in the right pane |
| Equipment | Choose one of the 48 equipment entries; native item artwork matches its category |
| Potion | Choose one of eight colors |
| Scroll | Choose one of eight scroll spells or the HMS Cape plans |
| Moonstone | One of eight starting buried-stone positions |
| Sandalwood box | Native quest pickup |
| Horse, Skiff, Frigate, Magic carpet | Native world objects; frigates retain hull controls |
| Corpse, Blood splatter | Native world object artwork and behavior |
| Trigger | Dungeon combat-room trigger, preserving the room metadata layout |

World objects have 31 non-party slots in Britannia. Underworld placement reserves
slots 28–31 for engine-created quest objects, leaving 27. NPC, sign and encounter
capacities still follow their native formats. Buried moonstones do not consume
world-object slots in their initial-state definition.

The Add menu deliberately excludes riding/transport animation frames, party
avatars and transient effect tiles. The amulet and shards spawned by Underworld
progression use engine-reserved slots, rather than ordinary Add placements.
Dungeon doors, traps, ladders and room links remain dungeon terrain features.
Overworld location landmarks are terrain plus entrance records, not new location
identities: this work does not expand the native set of settlements or dungeons.

## Starting settings

Game Rules now exposes date/time, karma, party count (read-only), active party
member, initial moon phases, general supplies, equipment, all 48 spell mixtures,
scrolls, potions, quest-item ownership and the existing progression flags. Party
membership is edited in **Party members** and stays coordinated with count and
active-member identity. Typed edits and opening the editor preserve unrelated
bytes and unusual inherited values.

Travel-state animation and combat bookkeeping remain internal. A starting
transport editor would need to coordinate the party tile, resident actor and
vehicle state together; exposing that byte as an arbitrary numeric field would
not be a reliable modding control. The new placement controls change position,
not transport state or location identity.

Other still-unexposed INIT.GAM data includes NPC killed/met history, last-harvest
bookkeeping, temporary spell durations and combat state. These bytes remain
lossless in projects and packages. NPC history needs a deliberate per-NPC UI;
it should not be confused with deleting a schedule record. Calendar changes do
not reset harvesting history automatically. These are remaining coverage gaps,
not claims that every byte of the starting-state format is now an editable setting.

New merchant/service identities and entirely new shops are not part of ordinary
NPC creation. Existing merchants can be edited or repositioned through their
map schedules, and existing shops have editable stock/prices and service text.
Creating another merchant/shop would require extending the engine's merchant
registry. Native signs cover world maps and settlements, not dungeon levels or
combat maps; those would need an additional runtime extension.
