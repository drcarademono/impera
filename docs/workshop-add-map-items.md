# Adding map items

Choose NPCs, Monsters or Special in the map tile picker, select an entry, and
click its destination. Use Select/Move to select an existing object and edit its
properties in the right pane, or drag it to another cell. Delete removes the
selected object and Undo restores the transaction. Terrain painting alone does
not invent object records or contents.

NPCs are placed on location maps; world-map conversational NPCs are not supported.
World monsters use roaming creature records, location monsters use NPC records,
and combat maps retain their encounter placement rules. Signs pair text with
visible terrain. Chests and containers use explicit contents, lock, trap and
refill settings. Container contents can belong to furniture or terrain without
changing its artwork. Dungeon features remain available through feature painting.

## Location Special placements (current behavior)

The tile picker's **Special** filter also works on location maps. It includes
vehicles, loose gold/food and other item pickups, moonstones, bodies, triggers,
signs and containers. Select/Move opens each object's properties in the right
pane. Quantities and item variants use the same native fields as world objects;
frigates start with their sails down, 99 hull and one skiff aboard. Place vehicles
on terrain suitable for their native movement rules.

Vanilla locations already contain non-NPC object records: Trinsic's skiff is one
example. Workshop-created objects use extended location slots (32 and above), so
they do not replace those original records. The engine now carries their item
properties into actors and remembers when they have been picked up or boarded.
Talking animals remain conversational NPCs rather than static objects.

Both body graphics have native descriptions identifying them as corpses. The
intact prone figure is **Corpse (intact)**; the red remains are **Corpse**, retaining
the game's searchable moldy-corpse behavior, including its loot and plague rules.
**Blood splatter** reuses the red graphic as decoration, without corpse searches,
loot or plague. It does not take interaction priority over another object on the
same tile.

Location triggers are invisible in-game and have a T marker in Workshop. Stepping
on their source changes up to two destination cells on the same floor to the
selected terrain tile. They fire once per location/floor visit; current trigger
state is retained in saves.

### Authored location object encoding

These placements require package version 18. Original `.NPC` records and the
`INPC0001` extension layout remain unchanged. An authored static object uses three
identical behavior bytes `0x80 | kind`, where `kind` is the stable MapItemKind ID
5–23. The conversation byte stores native actor property 5; schedule time byte 3
stores property 7. The three destinations agree for ordinary objects. A trigger
uses destination 0 as its source and destinations 1 and 2 as its targets.
Decorative blood is shape 31, property 5 = 255, property 7 = 0xb1. Untagged shape
31 retains native corpse behavior. Ordinary unmarked location records preserve
all existing schedule and conversation semantics.

## Pirate ships, whirlpools and quest actors

Special also offers Pirate ship, Whirlpool, Shadow Lord, Shard, Lord British’s
crown, Lord British’s sceptre and Lord British’s amulet. Pirate ships start with
the native 100 hull condition; frigates start with 99. Pirates and whirlpools use
the native movement, cannon/attack and whirlpool rules, including when placed in
a location. Winning a pirate encounter can leave a captured frigate with the
native hull and two skiffs aboard.

Choose the identity when placing a Shadow Lord or shard; Select/Move allows that
choice to be edited later. Shard pickup grants the corresponding native quest
item, as do the crown, sceptre and amulet. The engine's location-wide Shadow Lord
influence has one identity, so Workshop permits one placed Shadow Lord per
location. Its chosen identity applies on its floor, including the native NPC and
shop/dialogue effects. World Shadow Lords use the native world encounter rules.

These new location placements require package version 19. Stable placement IDs
24–30 extend the existing authored-object encoding. A Shadow Lord uses property
7 for its identity (0 Falsehood, 1 Hatred, 2 Cowardice); a shard uses property 5.
World objects retain their native eight-byte records. Existing reserved
Underworld quest slots are not overwritten.

Placed chests are drawn with chest artwork in the contents inspector as well as
in the other map modes. They remain object overlays rather than painted terrain,
so moving or removing a chest preserves the floor underneath. Empty unopened
chests remain visible in-game until opened.
