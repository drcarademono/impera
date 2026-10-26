# Impera mod packages

Impera automatically scans the **`Mods` folder beside its executable, AppImage or macOS app bundle**
at startup. Only files whose extension is `.imperamod` (case-insensitive) are
loaded, in deterministic bytewise filename order. There is no extraction or
execution of package content. Mounted resources are read-only startup snapshots;
read handles have the ordinary seek/read behavior expected by existing loaders.
Writable saves and engine settings never go through these overrides.

Release archives include that empty folder alongside both applications. For
AppImages, mods live beside the AppImage file, outside its read-only image. For
macOS, they live beside `Impera.app`, outside the signed bundle. The Ultima 5 data
folder remains the source of original resources used for package verification;
its old `Mods` folder is no longer scanned. Move existing packages to the new
application-side folder. Workshop Test Mod uses `U5D_MODS_DIR` to select its
isolated disposable session folder without loading or writing installed mods.

All entries are checked against the **unmodified original resources**, not a
previous mod's output. A malformed package, wrong base, or conflicting resource
rejects the **entire package**, with a diagnostic in `LOG.TXT`. This keeps related
resources such as `BRIT.DAT` / `DATA.OVL` together. The first valid package claiming
a resource wins; subsequent packages touching that resource are skipped even if
the byte ranges do not overlap. Rename/recombine packages or merge them in
Workshop rather than relying on silent last-wins behavior. Names/titles, loaded
counts and rejected packages are logged. Restart after changing the folder.

## Binary layout

All integers are unsigned little-endian 32-bit values; there is no padding.

| Header | Encoding |
| --- | --- |
| Magic/version | Eight ASCII bytes `IMOD0001` through `IMOD0020` |
| Title length | 1–256 UTF-8 bytes |
| Resource count | 1–512 |
| Title | Length bytes; embedded NUL forbidden |

Each resource follows in order:

| Resource field | Encoding |
| --- | --- |
| Filename length | 1–32 |
| Filename | Uppercase ASCII basename, no path components |
| Original size | Exact original resource byte count |
| Result size | 1–8 MiB |
| Original CRC-32 | Standard IEEE CRC-32 of original file bytes |
| Result CRC-32 | Standard IEEE CRC-32 of patched resource bytes |
| Patch count | Number of following spans |
| Each patch | Offset, length, then `length` payload bytes |

The result begins as the original bytes, truncated or zero-extended to result
size. Patches replace bytes in that buffer; they do not insert/shift bytes.
Spans must be nonempty, sorted, nonoverlapping and entirely within the result.
Zero patches are allowed (for example, a truncation). Trailing package bytes,
duplicate resource names and checksum mismatches are rejected.

CRC-32 detects accidental corruption and incompatible originals; it is not an
authentication/signature system. Packages contain game-resource edits, and valid
checksums do not by themselves guarantee that an arbitrary game's data is valid.
Workshop validates the supported resource formats before saving/exporting.

Version 5 adds native dialogue with explicit quotation marks and inherits all
version 4 container, version 3 table and version 2 narrative capabilities.
Workshop selects it when a modified TLK response uses the narration extension.
Older engines reject these packages with an update diagnostic, rather than
displaying extension bytes as dialogue. Packages without this feature retain
their existing minimum version.

An explicit-quotation response starts with the four non-NUL bytes `FC FD FC FB`,
after the two-byte question label prefix when present. This reserved prefix is
recognized only at response entry; the engine omits its automatic opening and
closing quotation marks. Workshop writes the actual quotes around speech and
writes narration unquoted. This supports leading/trailing narration, narration-only
responses, prompts, fallback answers and matching answers without changing their
control flow. The prefix is invisible in the authoring UI and Playtest. Name remains
an identity field, not a narration response. Original unmarked DOS dialogue uses
the original quoting behavior unchanged.

Allowed resources: uppercase basenames ending `.DAT`, `.NPC`, `.TLK`, `.16`,
`.CH`, `.HCS`, `.CBT`, plus `DATA.OVL`, `INIT.GAM`, `INIT.OOL`, `BRIT.OOL` and
`UNDER.OOL`. Drivers, DOS executable overlays other than the Britannia index,
settings, paths and saved-game files cannot be overridden. This is resource data
modding, not replacement executable code.

Limits: 32 MiB encoded package, 32 MiB combined decoded resources per package,
8 MiB per resource, 128 loaded packages, 64 MiB combined mounted resource data.
Failures leave original resources available and do not write to the installation.
Sparse packages omit unchanged resources and unchanged bytes where possible;
compressed graphics edits can necessarily change much of the compressed stream.

The shared parser/checksum implementation is `src/mod/package.c`, used by both
the native Workshop and Impera. `src/mod/runtime.c` mounts packages, and
`FILE_Open` applies overrides only to bare, read-only resource names. Explicit
paths and writable handles retain their existing behavior. The legacy DOS
Makefile has no SDL mod loader; this feature belongs to Impera's native port.

## World maps and starting objects

Impera now uses a replaced `DATA.OVL`'s Britannia chunk index at `0x3886` for both
normal gameplay and the expanded fullscreen viewport. Packages containing a map
or index edit are checked against the effective `BRIT.DAT`/index pair, including
earlier non-conflicting packages. Invalid chunk references or truncated data
reject the whole candidate package. Without an overlay override, the original
compiled index remains the default; other DOS overlay tables are not interpreted.
Prefer exporting the paired map/index edits together through Workshop.

Starting object overrides apply when creating a character or transferring one
from Ultima IV, before the normal modern initial save is serialized:

- Britannia starts empty unless a package explicitly replaces `BRIT.OOL`.
- Underworld starts from effective `INIT.OOL`, unless a package explicitly
  replaces `UNDER.OOL`, which takes precedence.
- Each object list must contain exactly 32 eight-byte records (256 bytes).
- A world start uses that world's template for its active object list too;
  settlement actors still come from the starting game state (`INIT.GAM`).

Existing runtime world files do not change these new-game templates. Existing
modern saves retain their saved party and object state; templates are not applied
again when loading a save or traveling. Normal world-save initialization for
first-time installation remains separate. Terrain resource replacements still
apply to the game normally; they are not restricted to new games.

Packages depending on this behavior require an engine build with Workshop Phase 1
support. The package encoding is unchanged, so older builds can decode a package
without implementing these effects. There is no minimum-engine-version field in
`IMOD0001`; version enforcement is reserved for a future format/schema extension.
Workshop's package preview reports these compatibility limits.

## Special scenes

`MISCMAPS.DAT` replacements preserve the original scene format. Both Workshop and
the native loader check the creation block (at `0x2c0`, bytecode at block `0x200`)
against the same instruction decoder: buffer capacity, actor references, tile
coordinates, positive waits, matched non-nested repeats and a reachable restart.
Unknown one-byte operations follow the original interpreter's no-op behavior;
Workshop preserves them and disables structural/parameter edits for that stream.

Extended Blackthorn, shrine, Codex and final-rescue text uses the existing
`TEXT.OVR` schema with new verified scene IDs. Overrides are resolved at the actual
message print, never by replacing a bulk message-bank read. Other entries still
use the original bank. These IDs require Phase 6 Impera: older narrative-enabled
builds reject unknown entries. DAT-only map/message edits retain package version 1.

## Companion versions

Version 2 permits the verified `TEXT.OVR` narrative companion. Version 3 also
permits [engine table overrides](workshop-engine-tables.md) in `RULES.TBL` and
continues to accept narrative and ordinary resource entries. Companions have an
empty original base and zero base CRC; the engine validates their schemas before
mounting the package. Ordinary resource checks remain unchanged in all versions.
Older engines reject unsupported versions explicitly. Current Impera retains
version 1 and 2 compatibility.

### Illustrated cutscene pages (version 6)

`CUTSCENE.OVR` replaces either or both illustrated page sequences without editing
original DAT files. It requires `IMOD0006`; it has an empty base, like the other
virtual companions. Schema `ICUT0001` is followed by little-endian u32 introduction
and ending counts. A zero count inherits that original sequence; an override has
1–128 pages. At least one sequence must be overridden.

Introduction records precede ending records. Each record contains a u32 layout
index (0–20 for the Introduction, 0–5 for the Ending), a u32 text length including
the terminator, and that many bytes of NUL-terminated ASCII. The limit is 2,000
bytes including the terminator. Embedded NULs, unsupported controls, characters
above `{`, invalid layouts, truncated records and trailing bytes reject the whole
package. Layouts reference the original artwork arrangement and text exclusions;
the editor and engine share proportional wrapping and positioning. The portal
layout uses its first newline to separate its upper and lower text regions.

A cutscene override takes precedence over the original sequence, including any
`TEXT.OVR` ending entries. An inherited Ending still honors `TEXT.OVR`. As with
other whole-resource companions, overlapping cutscene packages conflict rather
than silently overwriting one another. Original 21-scene Introduction and six-page
Ending playback, including their transitions, remain the fallback.

### Per-page cutscene placement (version 7)

`IMOD0007` adds `ICUT0002` while retaining all previous package and cutscene
schemas. Schema 2 keeps the sequence counts and page headers from schema 1, but
inserts twelve little-endian u32 values between each page's layout/text-length
header and its text:

1. Custom placement enabled (0 or 1).
2. Artwork anchor X.
3. Artwork anchor Y.
4. Text left edge outside the wrap band.
5. Text right edge outside the wrap band.
6. Text left edge inside the wrap band.
7. Text right edge inside the wrap band.
8. Wrap band top.
9. Wrap band bottom.
10. First text line X.
11. First text line Y.
12. Lower portal paragraph Y.

A disabled custom flag inherits the selected original layout. Artwork anchors
must lie within the 320 × 200 page. Text edges must form nonempty intervals within
0–320, the wrap band must be ordered within 0–200, text X within 0–319, and text
Y/lower paragraph Y within 0–191. All encoded coordinate values are bounded by
320, even on records whose custom flag is disabled. Invalid values reject the
entire package. An inactive wrap band can use top = bottom = 200.

Custom artwork placement translates the entire original illustration group,
including decorative lettering, portal art and secondary/revealed frames. Drawing
clips to the game page, and the reveal region is clamped before page transfers.
The same placement object and font-layout kernel drive Workshop previews and
Impera playback. Layout changes are per page; duplication copies them and reordering
moves them with their text. If no page needs custom placement, Workshop emits the
original schema 1 / package version 6 instead.

### Image and text-box compositions (version 8)

`IMOD0008` supports `ICUT0003` alongside the earlier schemas. All integers are
little-endian u32 unless specified otherwise. The header contains the magic, the
introduction and ending page counts, and an asset count. A sequence count of zero
inherits the original sequence; a specified sequence has 1–128 pages. At least one
sequence is specified. The shared validator rejects resources larger than 8 MiB.

Up to 256 embedded assets follow. Each asset contains width, height, UTF-8 label
byte length, non-NUL label bytes (1–256), and width × height × 4 straight RGBA bytes.
Dimensions are 1–1024 on each axis. Assets are deduplicated across both sequences.
Imported game images are snapshots of the chosen original or edited artwork and
have the same representation as imported PNGs. There are no external paths.

Pages follow in introduction/ending order. Each starts with an image count (0–64)
and text-box count (0–32). Image records contain asset index, X, Y. X/Y anchor the
native-size image within 320 × 200; drawing outside the page clips safely.
Image order is back to front. All text boxes are drawn after all images.

Each text-box record contains fourteen words followed by NUL-terminated text:
X, Y, width, height, preserved-flow flag, eight preserved-flow coordinates, and text
byte length including its terminator (1–2000). Rectangles must be nonempty and fit
inside 320 × 200. Text permits LF, CR, and printable ASCII through `{`, with no
embedded NUL. Eight signed two's-complement coordinates (bounded to −320…320)
preserve original text layout during conversion: left/right outside and inside the
wrap band, band top/bottom, start X/Y, all relative to the text-box origin. When the
flag is zero these coordinates are ignored and normal rectangular wrapping is used.
Text edits and moves preserve this flow. Resizing adjusts both right margins by
the width change while preserving indentation and the wrap band. Ordinary text boxes wrap overlong words without silently discarding glyphs.
Text clips to its rectangle; Workshop reports overflow without deleting source text.

PNG colours and partial alpha are preserved. Impera retains full RGB pixel provenance
through native framebuffer copies independently of the selected map tileset. Plain
UI drawing clears that provenance normally. Package validation checks all counts,
indices, byte ranges, dimensions, text and flow coordinates before mounting; malformed
or truncated data rejects the entire package. Older engines reject version 8 explicitly.

## Custom tile libraries (package version 9)

`IMOD0009` adds `TYPES.OVR` and `TYPEMAP.OVR`. Both use an empty original base,
are validated by the shared C implementation in `src/mod/types.c`, and remain
subject to the existing package/resource memory limits. Older engines reject
version 9 rather than reading custom tile IDs as DOS bytes.

`TYPES.OVR` (`ITYPES01`) contains a 16-byte library identity, sorted definitions,
and embedded RGBA animation frames. IDs 0–255 override original terrain;
256–511 identify original actor appearances; 512–65535 identify additions.
Kinds 0/1/2 distinguish terrain, NPC types and monster types. There are at most 1024 definitions,
64 frames per artwork variant, and 8 MiB per library. Frames are 16×16 or 32×32, with 14×16 also supported for Apple II,
with durations of 10–60000 milliseconds. Artwork variants fall back to the tile's
default artwork. Raw artwork preserves explicit alpha and opaque black. When
Transparent Sprites is enabled, actors marked Transparent also key black and use
the native outline across their full footprint. Physical properties belong to
the definition, not to individual animation frames.

`TYPEMAP.OVR` (`ITYMAP01`) contains sparse placements keyed by map, floor, and
coordinates, or encounter bank and encounter index. Its UUID must match the
library shipped in the same package. Custom terrain works on Britannia,
Underworld, location floors, and combat rooms; dungeon feature nibbles retain
their existing format. There are at most 65536 placement/state records. The
exact encodings and bounds are documented in `src/mod/types.h`.

Placements preserve the original terrain bytes underneath; custom IDs do not
consume DOS terrain IDs or Britannia chunk-index capacity. Runtime reads resolve
the authored native interaction profile and independent movement, sight,
projectile, travel, hazard, transparency, and masonry properties. Explicit state
transitions retain custom type identity, including a door's temporary open state
and automatic close. Container contents and sign text remain separate placed
instances, edited through their existing contextual map panels. Workshop moves
custom scenery and its contents together. During gameplay, pushing scenery that
has explicit contents is prevented, so contents cannot be orphaned at its old cell.

Libraries keep their UUID and local IDs through project and package roundtrips.
Deleting a referenced type is rejected. The existing first-resource-wins package
conflict rule applies: two independently exported tile libraries are not silently
combined. Combine their content in one Workshop project instead.

Mutable custom terrain state is appended to modern saves using `ITYSAV01`, after
any existing map-contents state. The original 4192-byte party layout is unchanged.
The extension stores stable library IDs, cell identities, transitions and door
restore state, with a checksum. Preflight validates it before installing a save
or changing the active party. Missing libraries, unknown types, corruption and
trailing data reject the load. Saves without the extension remain supported.

Version 10 (`IMOD0010`) enables independent NPC and monster definitions in
`TYPES.OVR`. Terrain-only libraries continue to export as version 9. Older
engines reject actor packages with the normal update diagnostic. Actor records
use `base` for the original behavior profile, `stats[8]` for native combat stats,
`actorFlags` for verified abilities (unknown bits are preserved), `moveProfile`,
`range`, `projectileAppearance` and the original companion fallback. Their
`result[4]` references mean on-defeat transformation, summoned type, division type,
and encounter companion; zero keeps the native/default rule. Builtin actor
references are 320–508 in increments of four; custom actor references use their
independent IDs. Actor artwork uses the same frame encoding and tileset fallback
as terrain. Properties, artwork and identity are independent.

`TYPEMAP.OVR` kinds 3/4/5/6 reference location NPC slots, world actor slots,
BRIT.CBT spawn slots and DUNGEON.CBT spawn slots. For these records `x` is a slot
and `y` is zero; location slots use map 1–32 and floor zero (their schedules own
floor changes), world slots use map zero and floor 0/-1, encounter slots use the
room index and floor zero. Location/world actor references survive encounter
resets and saves. Encounter references reset with the selected encounter. The
same library UUID and shared validation protect terrain and actor references.

### Multi-tile actors (package version 11)

`IMOD0011` adds rectangular actor footprints and separate world artwork to
`TYPES.OVR`. Bytes 253–254 encode width minus one and height minus one (each 0–1);
byte 255 remains reserved. Non-actor and original-ID records retain a 1×1 footprint.
Frame variants 0–5 describe local/combat artwork, with pixel dimensions equal to
cell dimensions multiplied by the footprint. Variants 6–11 describe single-cell
world artwork in the same tileset order. Each appearance has its own default
variant and animation length. Larger actors require default world artwork.
Packages containing any world-artwork variants require version 11, even for 1×1
actors. Older versions reject these records; ordinary legacy packages remain valid.

Actor instances retain one anchor coordinate, type identity, health pool and turn.
Footprints derive from the mounted type definition, so saved positions and the
existing type-state save format do not duplicate geometry. Britannia and Underworld
always use the single-cell appearance and collision footprint.

### Version 12: additional location NPCs

`IMOD0012` permits extended `.NPC` banks and 16-bit location actor instance IDs.
The original 4608-byte bank remains unchanged in structure. Optional trailing
bytes are `INPC0001`, a little-endian 32-bit record count, four reserved zero bytes,
and sorted 24-byte records: settlement index (0–7), zero byte, slot LE16,
schedule[16], native appearance byte, conversation byte, two reserved zero bytes.
Within each settlement, extension slots start at 32 and are contiguous; deletion
clears a record instead of renumbering later NPCs. Slots below 32 remain in the
original bank. Slots 1–32767 represent NPCs; slot zero is reserved.

For `TYPEMAP.OVR` kind 3, x and y are now the low and high bytes of the stable NPC
slot. Other placement kinds retain their existing coordinate semantics. Version
12 is required for any binding beyond slot 31. Unsupported older engines reject
the package before mounting it.

Saved games retain the original 4192-byte base. An optional `INPSAV01` prefix
before the contents/type trailers stores additional killed/met flags and current
NPC/actor/schedule/movement state, with explicit little-endian words and a CRC.
The entire trailer is validated before the active game is modified. Vanilla saves
without this prefix reset extension state normally.

`IMOD0013` adds the custom actor animation choice in definition byte 255:
0 follows the vanilla base creature’s pose pattern; 1 plays a continuous loop.
Original actor overrides and terrain retain zero in this byte. Inherited actors
use the engine’s native animation state, random holds, resting poses and delays,
independently for each placed actor. The same interpreter drives Workshop’s
preview at the normal idle update cadence. One to four artwork poses map to the
native four pose slots, repeating when fewer are supplied. Animations longer
than four poses use continuous playback so every frame is displayed; Workshop
selects that mode when adding/importing a longer sequence. Existing longer
sequences retain continuous playback. World artwork and alternative tilesets
retain the selected actor pose, including DOS fallback. Frame durations remain
in the file format for continuous sequences; Workshop exposes no timing numbers
or animation scripting. Workshop exports custom actor definitions as version 13
so older engines reject these packages instead of silently ignoring inherited
animation. Older packages remain readable by the updated engine.

## Extended dialogue banks (version 14)

`IMOD0014` adds `ITLK0001` dialogue banks when a conversation exceeds 1,024 bytes
or a bank exceeds the original signed DOS offset range. Ordinary conversations
continue to use the original TLK layout. Workshop chooses the format automatically.
The extension works with the existing speech/narration controls and NPC identities.

An extended `.TLK` resource starts with eight ASCII bytes `ITLK0001`, a little-endian
32-bit conversation count, and one `{id, offset, length}` record per conversation.
All three record fields are unsigned little-endian 32-bit integers. Offsets are
absolute byte offsets in the bank. IDs are unique and retain the original 16-bit
identity range; the table retains its 127-conversation capacity. Payloads contain
unchanged dialogue bytecode. Records must be ordered and non-overlapping, with
nonempty payloads entirely inside the resource.

Impera loads the selected conversation with its actual byte length instead of
copying it into the shared 1,024-byte scratch buffer. Workshop's editor and native
playtest also use the actual conversation length. The existing package resource
limit of 8 MiB per bank still applies; there is no separate 1,024-byte conversation
limit. Older engines reject version 14 packages rather than misreading their banks.

## Conversation opening settings (version 15)

`IMOD0015` adds the **Skip opening dialogue** setting, shared by every NPC using
the conversation. It skips the random “I am called…” introduction and the
already-introduced greeting, while retaining the description, Name response,
topics, and introduction-state flag.

The setting is encoded as the four-byte prefix `FC FD FC FA` at the start of the
conversation's Description entry. It may precede the explicit-quotation prefix.
Workshop hides the prefix from visual editing and preserves it when editing the
description. Banks with this setting require package version 15, including banks
that otherwise fit the original DOS TLK layout.

## Gargoyle speech (version 16)

`IMOD0016` adds the shared **Gargoyle speech** conversation setting. The engine's
automatic “I am called…” introduction and “My name is…” Name response use
“To be called…” instead. Authored text and greetings remain unchanged.

The setting is encoded as `FC FD FC F9` at the start of the Description entry.
It can be combined with the version 15 opening setting, in either order, before
any explicit-quotation prefix. Workshop hides these metadata bytes from visual
editing. Banks using gargoyle speech require package version 16.

## Location names (version 17)

`IMOD0017` adds `LOCATIONS.OVR`, an optional package-only companion with an empty
original base. Its eight-byte magic is `ILOC0001`, followed by 42 fixed 64-byte
slots. Slots 1–40 use the engine's location IDs (32 locations, then eight
dungeons); slot 0 is Britannia and slot 41 is the Underworld. Each slot contains
up to 63 printable ASCII characters, a NUL terminator, and zero padding. Empty
slots retain the original name. Leading/trailing spaces, control characters,
nonzero padding, and incorrect resource sizes are rejected.

Workshop's map-header pencil renames the current location across every floor.
The dungeon header offers the same control. Names participate in undo/redo,
project persistence, navigation, NPC location filters, and entrance markers.
Impera uses overrides for entrance announcements and save-slot location names;
renamed dungeon entrances also use the new name when Look identifies them.
Authored dialogue, signs, and other prose are not rewritten.


## Extended maps and locations (version 18)

`IMOD0018` adds the package-only `MAPS.OVR` catalog (empty original base).
Native resource banks remain intact. Each overridden world, location floor, or
combat board has its own width and height, independently 1–256 tiles. Coordinates
remain bytes. Custom terrain identities continue to use `TYPEMAP.OVR`.

`MAPS.OVR` has a 16-byte header: `IMAPS001`, a u32 record count (at most 512),
and a zero u32. Records are sorted by kind, ID, then signed floor. Each consists
of a 128-byte descriptor followed by width × height terrain bytes. The catalog
is limited to 8 MiB, including descriptors.

| Descriptor offset | Field |
| --- | --- |
| 0 | u32 total record length |
| 4–5 | u8 kind and ID; kinds 0 world, 1 location, 2 BRIT.CBT, 3 DUNGEON.CBT |
| 6 | u16 native page index; zero for worlds and new locations |
| 8–9 | i8 floor; u8 background tile |
| 10, 12 | u16 width and height |
| 14 | i8 entrance world: 0 Britannia, −1 Underworld, −2 no entrance |
| 15–16 | u8 world entrance X and Y |
| 17–18 | u8 arrival X and Y |
| 19 | u8 terrain originally underneath a new location's entrance |
| 20 | Combat marker mask present (0 or 1); zero for other maps |
| 21–23 | Zero reserved bytes |
| 24–87 | NUL-terminated printable ASCII name, at most 63 characters |
| 88–119 | NUL-terminated uppercase resource basename |
| 120–127 | Combat marker bits: party 0–23, monsters 24–39, triggers 40–47, destinations 48–63; zero without a mask |

World IDs are 0 Britannia and 1 Underworld. Native location IDs remain 1–32;
combat IDs are native board indices. New locations use IDs 42–127 excluding
64–66, which belong to engine screens. Each new location has independent
`LOCnnn.NPC` and `LOCnnn.TLK` resources with empty original bases. `LOCnnn.DAT`
is a virtual Workshop route: terrain lives in the catalog, and an empty DAT
file is never exported. New locations initially have one ground floor.
Duplicate identities, entrances, page aliases, missing companion banks and
out-of-bounds authored placements are rejected.

Signs at extended coordinates or in new locations use `ISIG0001` in SIGNS.DAT:
eight-byte magic, a u32 record count, then the existing map/floor/X/Y bytes and
NUL-terminated glyph text per record. Original trailer bytes are retained.
The engine reads each matching sign with its full location ID; native sign
resources retain their original encoding when no extension is needed.

Impera stores changed extended terrain in an optional `IMPSAV01` prefix before
the existing NPC and contents/type save trailers. It has a 24-byte header:
magic, u32 payload size, u32 record count, u32 CRC32 and a zero u32. The payload
contains only changed map records. Descriptors must match mounted maps exactly
before any saved terrain is applied. An absent prefix resets terrain to the mod
baseline. Old saves remain readable. NPC introduction/death flags for new
location IDs use the optional `INPSAV02` NPC trailer, while native-only flags
continue to use `INPSAV01`.

In Workshop, Resize changes the selected map or floor as one undo operation.
Shrinking refuses to crop placed content; move it first. The World tile-picker
filter includes native and custom entrances on either world, keeping the world map open. NPCs and
Monsters filters place actors, including custom types, without painting terrain.
Special replaces the Add menu for other supported map items.
Select / Move and the Positions pane edit arrivals and entrances, switch their world, and navigate between them.
Moving an entrance restores the terrain underneath its old marker. Creation,
terrain, NPC/dialogue companions, project persistence and package import/export
participate in the same history.


## Location special actors and quest objects (version 19)

`IMOD0019` preserves all version 18 schemas and adds authored location object
kinds 24–30 to `.NPC` extension records: pirate ship, whirlpool, Shadow Lord,
shard, crown, sceptre and Lord British’s amulet. As with the earlier placed
objects, all three behavior bytes are `0x80 | kind`. Shape and property values
are validated together. Static pickups and bodies do not run NPC AI; pirates and
whirlpools use the native world actor routines on their location floor, while a
Shadow Lord uses native hostile NPC behavior. Shadow Lord identity is property
7 (schedule byte 15), and shard identity is property 5 (the conversation byte).
Both identities range from 0 to 2. A location has at most one authored Shadow
Lord to match the engine's single active location-wide influence. Consumed-state
flags and save records reuse the existing extended NPC state. Earlier package
versions reject these new location kinds rather than treating them as dialogue.


## Resized dungeon floors (version 20)

`IMOD0020` retains previous formats and adds MAPS.OVR kind 4. Its map ID is
33–40, floor is 0–7, resource is DUNGEON.DAT, and page is
`(map ID - 33) * 8 + floor`. Width and height independently range from 1 to 256.
Cells are native dungeon feature bytes, not terrain tiles. No entrance or combat
marker metadata is used. The native DUNGEON.DAT remains 4096 bytes. Dungeon
contents coordinates are validated against these dimensions, or 8×8 for floors
without an override. Mutations persist through the existing IMPSAV01 map-state
trailer. Each floor preserves vanilla toroidal wrapping at its new dimensions.

`IMOD0021` adds independent dungeons. MAPS.OVR kind-4 records may use unused map
identities 42–127 (excluding reserved 64–66), with all eight floors required. Floor
pages append contiguously after the native 64 pages. Floor zero stores entrance,
arrival, name, and underlay using the existing position fields. Bytes 21–22 hold a
16-aligned combat-room bank base, from 112 through 240; other floor records repeat
that base. The corresponding sixteen DUNGEON.CBT rooms must accompany the catalog.
Town and dungeon records cannot share an identity, and distinct new dungeons
cannot share a combat-room bank. Dungeon floor bytes retain the native feature
encoding. CONTENTS.OVR supports the additional dungeon encounter identities.

`IMOD0022` widens combat-room identities to 16 bits. MAPS.OVR kind-2/3 records
store the high identity byte in byte 23; the page field carries the matching full
identity. Kind-4 room-bank bases may range through 65520. TYPEMAP.OVR records use
byte 5 for the high map byte on combat terrain and actor bindings, leaving bytes
6–7 reserved. CONTENTS.OVR already has a 16-bit map field and now accepts that range
for dungeon rooms. The sparse map catalog accepts up to 4096 records, within its
existing 8 MiB budget. Narrow records retain their wire layout and old packages
and saves remain readable. New wide data requires the updated engine.

`IMOD0023` supports portable map imports. Ground-floor kind-1 and kind-4 records
may supply entrance and arrival metadata for native locations and dungeons too.
New location resources can have contiguous virtual pages for a basement and
floors 0–3, with entrance metadata on floor zero only. CONTENTS.OVR refill modes
3 and 4 mean “while the party has no skeleton keys” and “while the party has no
glass sword,” allowing vanilla conditional discoveries to move without retaining
an unrelated native discovery identity.

`IMPSAV02` retains the map-state header and cell records from `IMPSAV01`, using
header bytes 20–23 for a native portal's actual return map, world, X, and Y. It may
have zero changed cell records when only the return point needs saving. Native
portals remember the world actually entered from, including across save/load;
ordinary saves continue using `IMPSAV01`. Both versions remain readable.

### Town gates (`IMOD0024`)

`GATES.OVR` is an empty-base companion resource. Its `IGAT0001` header holds a
little-endian 32-bit record count after the eight-byte signature. Each sorted
16-byte record contains map ID, signed floor, X, Y, kind (1 portcullis, 2
drawbridge), behavior (0 decorative, 1 scheduled, 2 always open, 3 always
closed), open tile, closed tile, opening hour, closing hour, and six zero bytes.
Records sort by map, signed floor, Y, X; duplicate cells are rejected. The gate
coordinate is the lower cell, immediately south of the archway. Hours range
from 0 to 23; overnight schedules are supported, and equal hours mean closed.

Unlisted cells retain vanilla behavior: archways toggle the cell below them,
and bridge tiles retract to water at night. Native hours are 05:00–20:00.
The floor is initialized to the appropriate state on entry; subsequent updates
are deterministic and occur when the hour changes, including during rest.
Drawbridges retain vanilla's protection while the party stands on a bridge.
The former 32-cell drawbridge cap is removed. Overrides are confined to location
maps and validated against their floor dimensions.

### Black-pixel protection masks (`IMOD0025`)

Optional opaque black/white PNG sheets protect selected black sprite pixels from
color-key transparency. White protects black; black leaves existing rules intact.
They do not change nonblack colors, alpha transparency, sprite characteristics,
or draw order. Missing masks behave as entirely black sheets.

| Resource | External tileset-directory filename | Dimensions |
|---|---|---|
| `MASK_DOS.PNG` | `Ultima_5_Mask_DOS.png` | 512 × 256 |
| `MASK_AMI.PNG` | `Ultima_5_Mask_Amiga.png` | 512 × 256 |
| `MASK_APP.PNG` | `Ultima_5_Mask_AppleII.png` | 448 × 256 |
| `MASK_GRY.PNG` | `Ultima_5_Mask_Grayscale.png` | 512 × 256 |
| `MASK_SHW.PNG` | `Ultima_5_Mask_Sharp_World.png` | 512 × 512 |
| `MASK_SHN.PNG` | `Ultima_5_Mask_Sharp_NPC.png` | 512 × 512 |
| `MASK_CUS.PNG` | `Ultima_5_Mask_Custom.png` | 512 × 256 |

Slot ordering matches each corresponding artwork sheet. Sharp keeps separate
terrain and actor masks and preserves native subpixel precision. Custom atlas
masks use the standard 32-column layout, sampled alongside the selected artwork.
These resources use the existing empty-base PNG package mechanism. Workshop
provides blank masks and includes edited masks in packages and map transfers.

Custom artwork can also carry an independent mask per frame and variant. Bit 7
of the frame cell-width byte signals `width × height` mask bytes immediately
after RGBA data, each 0 or 255. Bits 0–6 retain the native cell width. Frame size
and iteration include these bytes. Masks follow artwork fallback and native
animation pose selection, and cover the complete multi-tile frame. Explicit
frame masks take precedence over sheet masks for edited vanilla slots; new
custom identities use their own frame masks.
