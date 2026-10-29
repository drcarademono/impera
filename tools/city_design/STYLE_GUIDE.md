# Ultima V architecture and furnishing style guide

This guide is grounded in a read-only Workshop export of **all 32 original
locations**, counting every exported floor. The game supplies examples, not new
Minoc quest data. Source exports remain outside the checkout; Minoc still uses
its authoritative `maps/Minoc.json`. The survey can be reproduced with the native
probe and Python script below.

- [All original ground-floor plans](style_evidence/original-towns.png)
- [Britain](style_evidence/original-location-02.png),
  [Fogsbane](style_evidence/original-location-09.png),
  [West Britanny](style_evidence/original-location-19.png)
- [All 512 DOS artwork slots](style_evidence/all-512-dos-slots.png)
- [Counts, chair orientations, descriptions and source hashes](style_evidence/survey.json)
- [Complete audited terrain catalogue](tile_catalogue.json)

The 512-slot atlas includes 256 terrain/furniture slots and 256 actor appearance
slots. Actor frames are not extra furniture. The complete 256-terrain descriptions
come from the existing audited catalogue; do not invent meanings from colors.
Do not use every tile simply to exhaust the palette: select appropriate objects
for each room, preserving rare/quest-specific art and gameplay meaning.

## Walls, corners and thresholds

Britain and the dwellings show continuous room enclosures, purposeful openings,
and properly closed returns. Fogsbane demonstrates a broad beveled hall, not a
series of one-cell teeth. Build the floor plan first; a union of overlapping
rectangles is not a finished wall plan.

Use ordinary `0x4f` for brick masonry and rough `0x4d` for the poor workers' buildings.
The labeled corner/crenellation slots `0x50–57` have upper-wall/castle artwork;
they are **not** generic substitutes for ground-level brick corners. Likewise
`0xd0–d3` are rendering masks, not ordinary architectural floor or wall pieces.

Cap concave joints so floor is not exposed to grass through a diagonal pinhole.
The toolkit's conservative construction rule uses eight-neighbor enclosure,
with explicit door/passages. This is an authoring rule inspired by the native
plans, not a claim that the runtime requires eight-neighbor walls. Bevels need
space: use a deliberate broad chamfer and an intact wall return, not alternating
floor/grass spikes. Avoid single-cell dead-end wall teeth in important facades.

Give a large building readable structure: outer mass, inner hall, room partitions,
connections and entrances. In the Minoc factory reference, use an octagonal work
hall, paired northern/southern rooms and paired western/eastern workshops. Keep
operator space, carrying lanes and room entrances clear. Ground-level room
partitions may differ from an open upper work loft, but ladders must align.

## Furniture vocabulary and assemblies

| Purpose | Actual terrain artwork | Use |
| --- | --- | --- |
| Seating | `90–93` chairs | Face a table, desk or operator station. Do not scatter chairs arbitrarily. |
| Work tables | `94/95/96` | Left end, optional middle and right end. Preserve complete table ends. |
| Meal tables | `9a/9b/9c` | Provisioned table pieces; suitable for break/dining areas, not machinery. |
| Beds | `ab/ac` | Complete left/right pair. Never leave half a bed. |
| Bedroom storage | `ad` dresser, `ae` end table, `af` trunk | Near sleeping places, but keep doors, ladders and shared aisles clear. |
| Personal fittings | `9d` mirror, `a8` vanity | In bedrooms/changing areas. `9e/9f` are different mirror states; do not introduce damaged-state art without purpose. |
| Study/shop | `a5` desk, `5c/5d` bookshelves | A seated work place and storage, not one isolated desk in an empty hall. |
| Domestic utilities | `bf` stove, `a9` pitcher, `a6/a7` barrel/cask | Furnish for actual living or storage needs. |
| Workshop | `58` anvil, `fc/fd` bellows frames, `bc` fireplace | A coherent work bay with an operator approach and safe carrying aisle. `80–83` are pendulum frames, not forges. |
| Lighting/ornament | `b0/b1` wall torches, `b2` brazier, `be` candelabrum, `5b` plant | Use a few at useful places; respect blocking and hazards. |
| Exterior | `a3` logs, `a2` hitching post, `a1` well, `bd` street lamp | Put them in yards/streets for a reason, not as random indoor filler. |

The survey counted **308 chairs**, **264 complete beds**, **38 dressers**,
**49 end tables**, **14 mirrors**, **17 desks**, **122 trunks**, **210 barrels**,
**63 stoves** and **41 candelabra** across the original location floors. Counts
include component/animation slots as stored, not deduplicated functional objects.
For example, a three-cell table is three tiles. These are evidence for a varied,
furnished world, not density quotas to apply mechanically to every room.

Native adjacent-table evidence confirms chair orientation:

| Chair | Faces/table lies | Survey adjacencies |
| --- | --- | --- |
| `90` | North | 107 north; 2 south outliers |
| `91` | East | 5 east |
| `92` | South | 65 south |
| `93` | West | 3 west |

Therefore use `92` above a table and `90` below it; use `91` to its left and `93`
to its right. The statistics identify a convention, not an assertion that all
chairs in the original game are arranged perfectly. Keep circulation around
furniture, and check the doorway's inside cell before placing a dresser or desk.

For a home, assemble a sleeping place with storage and a mirror/vanity, plus a
seated eating/work place and heating/cooking where space allows. For a poor
bunkhouse, use repeated complete bed pairs, modest shared dressers and end tables,
common fittings and clear aisles; do not turn it into a luxury bedroom collection.
For a workshop, separate raw stock, work surfaces, operator seats and finished
storage. Administrative and break rooms need their own fittings. More objects
alone do not make a room purposeful.

## Roads, shore and other ground

Use continuous brick paving `0x44` for the routes between the main civic,
commercial and industrial buildings, as seen in the original location plans.
Treat these as **explicit roads**, not unexplained interior floor patches outdoors.
Keep dirt spurs to the workers' housing. Native `0x20–26` are directional paths;
match their straights, corners and junctions. Do not fill a rectangle with `0x20`.

**Never use desert `0x07` to stand for a beach.** For this DOS-art Minoc, use
shallows inside the water boundary and native grass/mud bank transitions at the
land edge. Ordinary grass is appropriate where no exact corner bank exists.
Do not use white rendering fragments `0xe4–e7` as static sandy shore or floor.
Do not invent a beach tile, repaint the source art or silently substitute other
artwork to make a misleading preview. Preserve the actual coastline/river mask
and check water connectivity separately from pathfinding.

Wood floor outside a building is allowed only for the explicitly mapped shipyard
pier. Ordinary factory front yards are grass/bank/road, not indoor stone-floor
patches. Bridge direction must match the channel; no paved road should replace
open river water.

## Review procedure

1. Render original examples and inspect appropriate room/furniture details.
2. Plan a readable floor/room graph and wall returns before placing furnishings.
3. Reserve NPC destinations, quest contents, ladders, entrances and carrying aisles.
4. Place complete furniture assemblies according to room function and chair facing.
5. Render full maps and close-ups. Check corners, halves of furniture, object density,
   door approaches, road surfaces and shore semantics.
6. Run static accessibility, all-room and original-destination checks, then the native
   importer/pixel parity probe. A successful import is not a game play-test.

## Reproduce the survey

With the native probe built as described in README:

```sh
QT_QPA_PLATFORM=offscreen build/city-workshop-probe/city_workshop_probe \
  --survey-game 'Ultima 5' /tmp/new-u5-location-exports
python3 -m tools.city_design.style_survey --exports /tmp/new-u5-location-exports \
  --output /tmp/new-u5-style-evidence
```

Both output directories must be new. Neither command edits original game files.
